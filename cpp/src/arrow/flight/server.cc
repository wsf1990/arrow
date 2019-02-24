// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "arrow/flight/server.h"
#include "arrow/flight/protocol-internal.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <grpcpp/grpcpp.h>

#include "arrow/ipc/dictionary.h"
#include "arrow/ipc/reader.h"
#include "arrow/ipc/writer.h"
#include "arrow/memory_pool.h"
#include "arrow/record_batch.h"
#include "arrow/status.h"
#include "arrow/util/logging.h"

#include "arrow/flight/internal.h"
#include "arrow/flight/serialization-internal.h"
#include "arrow/flight/types.h"

using FlightService = arrow::flight::protocol::FlightService;
using ServerContext = grpc::ServerContext;

template <typename T>
using ServerWriter = grpc::ServerWriter<T>;

namespace pb = arrow::flight::protocol;

namespace arrow {
namespace flight {

#define CHECK_ARG_NOT_NULL(VAL, MESSAGE)                              \
  if (VAL == nullptr) {                                               \
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, MESSAGE); \
  }

class FlightMessageReaderImpl : public FlightMessageReader {
 public:
  FlightMessageReaderImpl(const FlightDescriptor& descriptor,
                          std::shared_ptr<Schema> schema,
                          grpc::ServerReader<pb::FlightData>* reader)
      : descriptor_(descriptor),
        schema_(schema),
        reader_(reader),
        stream_finished_(false) {}

  const FlightDescriptor& descriptor() const override { return descriptor_; }

  std::shared_ptr<Schema> schema() const override { return schema_; }

  Status ReadNext(std::shared_ptr<RecordBatch>* out) override {
    if (stream_finished_) {
      *out = nullptr;
      return Status::OK();
    }

    internal::FlightData data;
    // Pretend to be pb::FlightData and intercept in SerializationTraits
    if (reader_->Read(reinterpret_cast<pb::FlightData*>(&data))) {
      std::unique_ptr<ipc::Message> message;

      // Validate IPC message
      RETURN_NOT_OK(ipc::Message::Open(data.metadata, data.body, &message));
      if (message->type() == ipc::Message::Type::RECORD_BATCH) {
        return ipc::ReadRecordBatch(*message, schema_, out);
      } else {
        return Status(StatusCode::Invalid, "Unrecognized message in Flight stream");
      }
    } else {
      // Stream is completed
      stream_finished_ = true;
      *out = nullptr;
      return Status::OK();
    }
  }

 private:
  FlightDescriptor descriptor_;
  std::shared_ptr<Schema> schema_;
  grpc::ServerReader<pb::FlightData>* reader_;
  bool stream_finished_;
};

// This class glues an implementation of FlightServerBase together with the
// gRPC service definition, so the latter is not exposed in the public API
class FlightServiceImpl : public FlightService::Service {
 public:
  explicit FlightServiceImpl(FlightServerBase* server) : server_(server) {}

  template <typename UserType, typename Iterator, typename ProtoType>
  grpc::Status WriteStream(Iterator* iterator, ServerWriter<ProtoType>* writer) {
    // Write flight info to stream until listing is exhausted
    ProtoType pb_value;
    std::unique_ptr<UserType> value;
    while (true) {
      GRPC_RETURN_NOT_OK(iterator->Next(&value));
      if (!value) {
        break;
      }
      GRPC_RETURN_NOT_OK(internal::ToProto(*value, &pb_value));

      // Blocking write
      if (!writer->Write(pb_value)) {
        // Write returns false if the stream is closed
        break;
      }
    }
    return grpc::Status::OK;
  }

  template <typename UserType, typename ProtoType>
  grpc::Status WriteStream(const std::vector<UserType>& values,
                           ServerWriter<ProtoType>* writer) {
    // Write flight info to stream until listing is exhausted
    ProtoType pb_value;
    for (const UserType& value : values) {
      GRPC_RETURN_NOT_OK(internal::ToProto(value, &pb_value));
      // Blocking write
      if (!writer->Write(pb_value)) {
        // Write returns false if the stream is closed
        break;
      }
    }
    return grpc::Status::OK;
  }

  grpc::Status ListFlights(ServerContext* context, const pb::Criteria* request,
                           ServerWriter<pb::FlightGetInfo>* writer) {
    // Retrieve the listing from the implementation
    std::unique_ptr<FlightListing> listing;

    Criteria criteria;
    if (request) {
      GRPC_RETURN_NOT_OK(internal::FromProto(*request, &criteria));
    }
    GRPC_RETURN_NOT_OK(server_->ListFlights(&criteria, &listing));
    return WriteStream<FlightInfo>(listing.get(), writer);
  }

  grpc::Status GetFlightInfo(ServerContext* context, const pb::FlightDescriptor* request,
                             pb::FlightGetInfo* response) {
    CHECK_ARG_NOT_NULL(request, "FlightDescriptor cannot be null");

    FlightDescriptor descr;
    GRPC_RETURN_NOT_OK(internal::FromProto(*request, &descr));

    std::unique_ptr<FlightInfo> info;
    GRPC_RETURN_NOT_OK(server_->GetFlightInfo(descr, &info));

    GRPC_RETURN_NOT_OK(internal::ToProto(*info, response));
    return grpc::Status::OK;
  }

  grpc::Status DoGet(ServerContext* context, const pb::Ticket* request,
                     ServerWriter<pb::FlightData>* writer) {
    CHECK_ARG_NOT_NULL(request, "ticket cannot be null");

    Ticket ticket;
    GRPC_RETURN_NOT_OK(internal::FromProto(*request, &ticket));

    std::unique_ptr<FlightDataStream> data_stream;
    GRPC_RETURN_NOT_OK(server_->DoGet(ticket, &data_stream));

    // Write the schema as the first message in the stream
    FlightPayload schema_payload;
    MemoryPool* pool = default_memory_pool();
    ipc::DictionaryMemo dictionary_memo;
    GRPC_RETURN_NOT_OK(ipc::internal::GetSchemaPayload(
        *data_stream->schema(), pool, &dictionary_memo, &schema_payload.ipc_message));

    // Pretend to be pb::FlightData, we cast back to FlightPayload in
    // SerializationTraits
    writer->Write(*reinterpret_cast<const pb::FlightData*>(&schema_payload),
                  grpc::WriteOptions());

    while (true) {
      FlightPayload payload;
      GRPC_RETURN_NOT_OK(data_stream->Next(&payload));
      if (payload.ipc_message.metadata == nullptr ||
          !writer->Write(*reinterpret_cast<const pb::FlightData*>(&payload),
                         grpc::WriteOptions())) {
        // No more messages to write, or connection terminated for some other
        // reason
        break;
      }
    }
    return grpc::Status::OK;
  }

  grpc::Status DoPut(ServerContext* context, grpc::ServerReader<pb::FlightData>* reader,
                     pb::PutResult* response) {
    // Get metadata
    internal::FlightData data;
    if (reader->Read(reinterpret_cast<pb::FlightData*>(&data))) {
      // Message only lives as long as data
      std::unique_ptr<ipc::Message> message;
      GRPC_RETURN_NOT_OK(ipc::Message::Open(data.metadata, data.body, &message));

      if (!message || message->type() != ipc::Message::Type::SCHEMA) {
        return internal::ToGrpcStatus(
            Status(StatusCode::Invalid, "DoPut must start with schema/descriptor"));
      } else if (data.descriptor == nullptr) {
        return internal::ToGrpcStatus(
            Status(StatusCode::Invalid, "DoPut must start with non-null descriptor"));
      } else {
        std::shared_ptr<Schema> schema;
        GRPC_RETURN_NOT_OK(ipc::ReadSchema(*message, &schema));

        auto message_reader = std::unique_ptr<FlightMessageReader>(
            new FlightMessageReaderImpl(*data.descriptor.get(), schema, reader));
        return internal::ToGrpcStatus(server_->DoPut(std::move(message_reader)));
      }
    } else {
      return internal::ToGrpcStatus(
          Status(StatusCode::Invalid,
                 "Client provided malformed message or did not provide message"));
    }
  }

  grpc::Status ListActions(ServerContext* context, const pb::Empty* request,
                           ServerWriter<pb::ActionType>* writer) {
    // Retrieve the listing from the implementation
    std::vector<ActionType> types;
    GRPC_RETURN_NOT_OK(server_->ListActions(&types));
    return WriteStream<ActionType>(types, writer);
  }

  grpc::Status DoAction(ServerContext* context, const pb::Action* request,
                        ServerWriter<pb::Result>* writer) {
    CHECK_ARG_NOT_NULL(request, "Action cannot be null");
    Action action;
    GRPC_RETURN_NOT_OK(internal::FromProto(*request, &action));

    std::unique_ptr<ResultStream> results;
    GRPC_RETURN_NOT_OK(server_->DoAction(action, &results));

    std::unique_ptr<Result> result;
    pb::Result pb_result;
    while (true) {
      GRPC_RETURN_NOT_OK(results->Next(&result));
      if (!result) {
        // No more results
        break;
      }
      GRPC_RETURN_NOT_OK(internal::ToProto(*result, &pb_result));
      if (!writer->Write(pb_result)) {
        // Stream may be closed
        break;
      }
    }
    return grpc::Status::OK;
  }

 private:
  FlightServerBase* server_;
};

struct FlightServerBase::FlightServerBaseImpl {
  std::unique_ptr<grpc::Server> server;
};

FlightServerBase::FlightServerBase() { impl_.reset(new FlightServerBaseImpl); }

FlightServerBase::~FlightServerBase() {}

void FlightServerBase::Run(int port) {
  std::string address = "localhost:" + std::to_string(port);

  FlightServiceImpl service(this);
  grpc::ServerBuilder builder;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  impl_->server = builder.BuildAndStart();
  std::cout << "Server listening on " << address << std::endl;
  impl_->server->Wait();
}

void FlightServerBase::Shutdown() {
  DCHECK(impl_->server);
  impl_->server->Shutdown();
}

Status FlightServerBase::ListFlights(const Criteria* criteria,
                                     std::unique_ptr<FlightListing>* listings) {
  return Status::NotImplemented("NYI");
}

Status FlightServerBase::GetFlightInfo(const FlightDescriptor& request,
                                       std::unique_ptr<FlightInfo>* info) {
  std::cout << "GetFlightInfo" << std::endl;
  return Status::NotImplemented("NYI");
}

Status FlightServerBase::DoGet(const Ticket& request,
                               std::unique_ptr<FlightDataStream>* data_stream) {
  return Status::NotImplemented("NYI");
}

Status FlightServerBase::DoPut(std::unique_ptr<FlightMessageReader> reader) {
  return Status::NotImplemented("NYI");
}

Status FlightServerBase::DoAction(const Action& action,
                                  std::unique_ptr<ResultStream>* result) {
  return Status::NotImplemented("NYI");
}

Status FlightServerBase::ListActions(std::vector<ActionType>* actions) {
  return Status::NotImplemented("NYI");
}

// ----------------------------------------------------------------------
// Implement RecordBatchStream

RecordBatchStream::RecordBatchStream(const std::shared_ptr<RecordBatchReader>& reader)
    : pool_(default_memory_pool()), reader_(reader) {}

std::shared_ptr<Schema> RecordBatchStream::schema() { return reader_->schema(); }

Status RecordBatchStream::Next(FlightPayload* payload) {
  std::shared_ptr<RecordBatch> batch;
  RETURN_NOT_OK(reader_->ReadNext(&batch));

  if (!batch) {
    // Signal that iteration is over
    payload->ipc_message.metadata = nullptr;
    return Status::OK();
  } else {
    return ipc::internal::GetRecordBatchPayload(*batch, pool_, &payload->ipc_message);
  }
}

}  // namespace flight
}  // namespace arrow
