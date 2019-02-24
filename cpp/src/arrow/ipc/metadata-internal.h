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

// Internal metadata serialization matters

#ifndef ARROW_IPC_METADATA_INTERNAL_H
#define ARROW_IPC_METADATA_INTERNAL_H

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include "arrow/buffer.h"
#include "arrow/ipc/Schema_generated.h"
#include "arrow/ipc/dictionary.h"  // IYWU pragma: keep
#include "arrow/ipc/message.h"
#include "arrow/memory_pool.h"
#include "arrow/sparse_tensor.h"
#include "arrow/status.h"

namespace arrow {

class DataType;
class Schema;
class Tensor;
class SparseTensor;

namespace flatbuf = org::apache::arrow::flatbuf;

namespace io {

class OutputStream;

}  // namespace io

namespace ipc {

class DictionaryMemo;

namespace internal {

static constexpr flatbuf::MetadataVersion kCurrentMetadataVersion =
    flatbuf::MetadataVersion_V4;

static constexpr flatbuf::MetadataVersion kMinMetadataVersion =
    flatbuf::MetadataVersion_V4;

MetadataVersion GetMetadataVersion(flatbuf::MetadataVersion version);

static constexpr const char* kArrowMagicBytes = "ARROW1";

struct FieldMetadata {
  int64_t length;
  int64_t null_count;
  int64_t offset;
};

struct BufferMetadata {
  /// The relative offset into the memory page to the starting byte of the buffer
  int64_t offset;

  /// Absolute length in bytes of the buffer
  int64_t length;
};

struct FileBlock {
  int64_t offset;
  int32_t metadata_length;
  int64_t body_length;
};

// Read interface classes. We do not fully deserialize the flatbuffers so that
// individual fields metadata can be retrieved from very large schema without
//

// Retrieve a list of all the dictionary ids and types required by the schema for
// reconstruction. The presumption is that these will be loaded either from
// the stream or file (or they may already be somewhere else in memory)
Status GetDictionaryTypes(const void* opaque_schema, DictionaryTypeMap* id_to_field);

// Construct a complete Schema from the message. May be expensive for very
// large schemas if you are only interested in a few fields
Status GetSchema(const void* opaque_schema, const DictionaryMemo& dictionary_memo,
                 std::shared_ptr<Schema>* out);

Status GetTensorMetadata(const Buffer& metadata, std::shared_ptr<DataType>* type,
                         std::vector<int64_t>* shape, std::vector<int64_t>* strides,
                         std::vector<std::string>* dim_names);

// EXPERIMENTAL: Extracting metadata of a sparse tensor from the message
Status GetSparseTensorMetadata(const Buffer& metadata, std::shared_ptr<DataType>* type,
                               std::vector<int64_t>* shape,
                               std::vector<std::string>* dim_names, int64_t* length,
                               SparseTensorFormat::type* sparse_tensor_format_id);

/// Write a serialized message metadata with a length-prefix and padding to an
/// 8-byte offset. Does not make assumptions about whether the stream is
/// aligned already
///
/// <message_size: int32><message: const void*><padding>
///
/// \param[in] message a buffer containing the metadata to write
/// \param[in] alignment the size multiple of the total message size including
/// length prefix, metadata, and padding. Usually 8 or 64
/// \param[in,out] file the OutputStream to write to
/// \param[out] message_length the total size of the payload written including
/// padding
/// \return Status
Status WriteMessage(const Buffer& message, int32_t alignment, io::OutputStream* file,
                    int32_t* message_length);

// Serialize arrow::Schema as a Flatbuffer
//
// \param[in] schema a Schema instance
// \param[in,out] dictionary_memo class for tracking dictionaries and assigning
// dictionary ids
// \param[out] out the serialized arrow::Buffer
// \return Status outcome
Status WriteSchemaMessage(const Schema& schema, DictionaryMemo* dictionary_memo,
                          std::shared_ptr<Buffer>* out);

Status WriteRecordBatchMessage(const int64_t length, const int64_t body_length,
                               const std::vector<FieldMetadata>& nodes,
                               const std::vector<BufferMetadata>& buffers,
                               std::shared_ptr<Buffer>* out);

Status WriteTensorMessage(const Tensor& tensor, const int64_t buffer_start_offset,
                          std::shared_ptr<Buffer>* out);

Status WriteSparseTensorMessage(const SparseTensor& sparse_tensor, int64_t body_length,
                                const std::vector<BufferMetadata>& buffers,
                                std::shared_ptr<Buffer>* out);

Status WriteFileFooter(const Schema& schema, const std::vector<FileBlock>& dictionaries,
                       const std::vector<FileBlock>& record_batches,
                       DictionaryMemo* dictionary_memo, io::OutputStream* out);

Status WriteDictionaryMessage(const int64_t id, const int64_t length,
                              const int64_t body_length,
                              const std::vector<FieldMetadata>& nodes,
                              const std::vector<BufferMetadata>& buffers,
                              std::shared_ptr<Buffer>* out);

static inline Status WriteFlatbufferBuilder(flatbuffers::FlatBufferBuilder& fbb,
                                            std::shared_ptr<Buffer>* out) {
  int32_t size = fbb.GetSize();

  std::shared_ptr<Buffer> result;
  RETURN_NOT_OK(AllocateBuffer(default_memory_pool(), size, &result));

  uint8_t* dst = result->mutable_data();
  memcpy(dst, fbb.GetBufferPointer(), size);
  *out = result;
  return Status::OK();
}

}  // namespace internal
}  // namespace ipc
}  // namespace arrow

#endif  // ARROW_IPC_METADATA_H
