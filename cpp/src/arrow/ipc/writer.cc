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

#include "arrow/ipc/writer.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <vector>

#include "arrow/array.h"
#include "arrow/buffer.h"
#include "arrow/io/interfaces.h"
#include "arrow/io/memory.h"
#include "arrow/ipc/dictionary.h"
#include "arrow/ipc/message.h"
#include "arrow/ipc/metadata-internal.h"
#include "arrow/ipc/util.h"
#include "arrow/memory_pool.h"
#include "arrow/record_batch.h"
#include "arrow/sparse_tensor.h"
#include "arrow/status.h"
#include "arrow/table.h"
#include "arrow/tensor.h"
#include "arrow/type.h"
#include "arrow/util/bit-util.h"
#include "arrow/util/checked_cast.h"
#include "arrow/util/logging.h"
#include "arrow/visitor.h"

namespace arrow {

using internal::checked_cast;
using internal::CopyBitmap;

namespace ipc {

using internal::FileBlock;
using internal::kArrowMagicBytes;

// ----------------------------------------------------------------------
// Record batch write path

static inline Status GetTruncatedBitmap(int64_t offset, int64_t length,
                                        const std::shared_ptr<Buffer> input,
                                        MemoryPool* pool,
                                        std::shared_ptr<Buffer>* buffer) {
  if (!input) {
    *buffer = input;
    return Status::OK();
  }
  int64_t min_length = PaddedLength(BitUtil::BytesForBits(length));
  if (offset != 0 || min_length < input->size()) {
    // With a sliced array / non-zero offset, we must copy the bitmap
    RETURN_NOT_OK(CopyBitmap(pool, input->data(), offset, length, buffer));
  } else {
    *buffer = input;
  }
  return Status::OK();
}

template <typename T>
inline Status GetTruncatedBuffer(int64_t offset, int64_t length,
                                 const std::shared_ptr<Buffer> input, MemoryPool* pool,
                                 std::shared_ptr<Buffer>* buffer) {
  if (!input) {
    *buffer = input;
    return Status::OK();
  }
  int32_t byte_width = static_cast<int32_t>(sizeof(T));
  int64_t padded_length = PaddedLength(length * byte_width);
  if (offset != 0 || padded_length < input->size()) {
    *buffer =
        SliceBuffer(input, offset * byte_width, std::min(padded_length, input->size()));
  } else {
    *buffer = input;
  }
  return Status::OK();
}

static inline bool NeedTruncate(int64_t offset, const Buffer* buffer,
                                int64_t min_length) {
  // buffer can be NULL
  if (buffer == nullptr) {
    return false;
  }
  return offset != 0 || min_length < buffer->size();
}

namespace internal {

class RecordBatchSerializer : public ArrayVisitor {
 public:
  RecordBatchSerializer(MemoryPool* pool, int64_t buffer_start_offset,
                        int max_recursion_depth, bool allow_64bit, IpcPayload* out)
      : out_(out),
        pool_(pool),
        max_recursion_depth_(max_recursion_depth),
        buffer_start_offset_(buffer_start_offset),
        allow_64bit_(allow_64bit) {
    DCHECK_GT(max_recursion_depth, 0);
  }

  ~RecordBatchSerializer() override = default;

  Status VisitArray(const Array& arr) {
    if (max_recursion_depth_ <= 0) {
      return Status::Invalid("Max recursion depth reached");
    }

    if (!allow_64bit_ && arr.length() > std::numeric_limits<int32_t>::max()) {
      return Status::CapacityError("Cannot write arrays larger than 2^31 - 1 in length");
    }

    // push back all common elements
    field_nodes_.push_back({arr.length(), arr.null_count(), 0});

    if (arr.null_count() > 0) {
      std::shared_ptr<Buffer> bitmap;
      RETURN_NOT_OK(GetTruncatedBitmap(arr.offset(), arr.length(), arr.null_bitmap(),
                                       pool_, &bitmap));
      out_->body_buffers.emplace_back(bitmap);
    } else {
      // Push a dummy zero-length buffer, not to be copied
      out_->body_buffers.emplace_back(std::make_shared<Buffer>(nullptr, 0));
    }
    return arr.Accept(this);
  }

  // Override this for writing dictionary metadata
  virtual Status SerializeMetadata(int64_t num_rows) {
    return WriteRecordBatchMessage(num_rows, out_->body_length, field_nodes_,
                                   buffer_meta_, &out_->metadata);
  }

  Status Assemble(const RecordBatch& batch) {
    if (field_nodes_.size() > 0) {
      field_nodes_.clear();
      buffer_meta_.clear();
      out_->body_buffers.clear();
    }

    // Perform depth-first traversal of the row-batch
    for (int i = 0; i < batch.num_columns(); ++i) {
      RETURN_NOT_OK(VisitArray(*batch.column(i)));
    }

    // The position for the start of a buffer relative to the passed frame of
    // reference. May be 0 or some other position in an address space
    int64_t offset = buffer_start_offset_;

    buffer_meta_.reserve(out_->body_buffers.size());

    // Construct the buffer metadata for the record batch header
    for (size_t i = 0; i < out_->body_buffers.size(); ++i) {
      const Buffer* buffer = out_->body_buffers[i].get();
      int64_t size = 0;
      int64_t padding = 0;

      // The buffer might be null if we are handling zero row lengths.
      if (buffer) {
        size = buffer->size();
        padding = BitUtil::RoundUpToMultipleOf8(size) - size;
      }

      buffer_meta_.push_back({offset, size + padding});
      offset += size + padding;
    }

    out_->body_length = offset - buffer_start_offset_;
    DCHECK(BitUtil::IsMultipleOf8(out_->body_length));

    // Now that we have computed the locations of all of the buffers in shared
    // memory, the data header can be converted to a flatbuffer and written out
    //
    // Note: The memory written here is prefixed by the size of the flatbuffer
    // itself as an int32_t.
    return SerializeMetadata(batch.num_rows());
  }

 protected:
  template <typename ArrayType>
  Status VisitFixedWidth(const ArrayType& array) {
    std::shared_ptr<Buffer> data = array.values();

    const auto& fw_type = checked_cast<const FixedWidthType&>(*array.type());
    const int64_t type_width = fw_type.bit_width() / 8;
    int64_t min_length = PaddedLength(array.length() * type_width);

    if (NeedTruncate(array.offset(), data.get(), min_length)) {
      // Non-zero offset, slice the buffer
      const int64_t byte_offset = array.offset() * type_width;

      // Send padding if it's available
      const int64_t buffer_length =
          std::min(BitUtil::RoundUpToMultipleOf8(array.length() * type_width),
                   data->size() - byte_offset);
      data = SliceBuffer(data, byte_offset, buffer_length);
    }
    out_->body_buffers.emplace_back(data);
    return Status::OK();
  }

  template <typename ArrayType>
  Status GetZeroBasedValueOffsets(const ArrayType& array,
                                  std::shared_ptr<Buffer>* value_offsets) {
    // Share slicing logic between ListArray and BinaryArray

    auto offsets = array.value_offsets();

    if (array.offset() != 0) {
      // If we have a non-zero offset, then the value offsets do not start at
      // zero. We must a) create a new offsets array with shifted offsets and
      // b) slice the values array accordingly

      std::shared_ptr<Buffer> shifted_offsets;
      RETURN_NOT_OK(AllocateBuffer(pool_, sizeof(int32_t) * (array.length() + 1),
                                   &shifted_offsets));

      int32_t* dest_offsets = reinterpret_cast<int32_t*>(shifted_offsets->mutable_data());
      const int32_t start_offset = array.value_offset(0);

      for (int i = 0; i < array.length(); ++i) {
        dest_offsets[i] = array.value_offset(i) - start_offset;
      }
      // Final offset
      dest_offsets[array.length()] = array.value_offset(array.length()) - start_offset;
      offsets = shifted_offsets;
    }

    *value_offsets = offsets;
    return Status::OK();
  }

  Status VisitBinary(const BinaryArray& array) {
    std::shared_ptr<Buffer> value_offsets;
    RETURN_NOT_OK(GetZeroBasedValueOffsets<BinaryArray>(array, &value_offsets));
    auto data = array.value_data();

    int64_t total_data_bytes = 0;
    if (value_offsets) {
      total_data_bytes = array.value_offset(array.length()) - array.value_offset(0);
    }
    if (NeedTruncate(array.offset(), data.get(), total_data_bytes)) {
      // Slice the data buffer to include only the range we need now
      const int64_t start_offset = array.value_offset(0);
      const int64_t slice_length =
          std::min(PaddedLength(total_data_bytes), data->size() - start_offset);
      data = SliceBuffer(data, start_offset, slice_length);
    }

    out_->body_buffers.emplace_back(value_offsets);
    out_->body_buffers.emplace_back(data);
    return Status::OK();
  }

  Status Visit(const BooleanArray& array) override {
    std::shared_ptr<Buffer> data;
    RETURN_NOT_OK(
        GetTruncatedBitmap(array.offset(), array.length(), array.values(), pool_, &data));
    out_->body_buffers.emplace_back(data);
    return Status::OK();
  }

  Status Visit(const NullArray& array) override {
    out_->body_buffers.emplace_back(nullptr);
    return Status::OK();
  }

#define VISIT_FIXED_WIDTH(TYPE) \
  Status Visit(const TYPE& array) override { return VisitFixedWidth<TYPE>(array); }

  VISIT_FIXED_WIDTH(Int8Array)
  VISIT_FIXED_WIDTH(Int16Array)
  VISIT_FIXED_WIDTH(Int32Array)
  VISIT_FIXED_WIDTH(Int64Array)
  VISIT_FIXED_WIDTH(UInt8Array)
  VISIT_FIXED_WIDTH(UInt16Array)
  VISIT_FIXED_WIDTH(UInt32Array)
  VISIT_FIXED_WIDTH(UInt64Array)
  VISIT_FIXED_WIDTH(HalfFloatArray)
  VISIT_FIXED_WIDTH(FloatArray)
  VISIT_FIXED_WIDTH(DoubleArray)
  VISIT_FIXED_WIDTH(Date32Array)
  VISIT_FIXED_WIDTH(Date64Array)
  VISIT_FIXED_WIDTH(TimestampArray)
  VISIT_FIXED_WIDTH(Time32Array)
  VISIT_FIXED_WIDTH(Time64Array)
  VISIT_FIXED_WIDTH(FixedSizeBinaryArray)
  VISIT_FIXED_WIDTH(Decimal128Array)

#undef VISIT_FIXED_WIDTH

  Status Visit(const StringArray& array) override { return VisitBinary(array); }

  Status Visit(const BinaryArray& array) override { return VisitBinary(array); }

  Status Visit(const ListArray& array) override {
    std::shared_ptr<Buffer> value_offsets;
    RETURN_NOT_OK(GetZeroBasedValueOffsets<ListArray>(array, &value_offsets));
    out_->body_buffers.emplace_back(value_offsets);

    --max_recursion_depth_;
    std::shared_ptr<Array> values = array.values();

    int32_t values_offset = 0;
    int32_t values_length = 0;
    if (value_offsets) {
      values_offset = array.value_offset(0);
      values_length = array.value_offset(array.length()) - values_offset;
    }

    if (array.offset() != 0 || values_length < values->length()) {
      // Must also slice the values
      values = values->Slice(values_offset, values_length);
    }
    RETURN_NOT_OK(VisitArray(*values));
    ++max_recursion_depth_;
    return Status::OK();
  }

  Status Visit(const StructArray& array) override {
    --max_recursion_depth_;
    for (int i = 0; i < array.num_fields(); ++i) {
      std::shared_ptr<Array> field = array.field(i);
      RETURN_NOT_OK(VisitArray(*field));
    }
    ++max_recursion_depth_;
    return Status::OK();
  }

  Status Visit(const UnionArray& array) override {
    const int64_t offset = array.offset();
    const int64_t length = array.length();

    std::shared_ptr<Buffer> type_ids;
    RETURN_NOT_OK(GetTruncatedBuffer<UnionArray::type_id_t>(
        offset, length, array.type_ids(), pool_, &type_ids));
    out_->body_buffers.emplace_back(type_ids);

    --max_recursion_depth_;
    if (array.mode() == UnionMode::DENSE) {
      const auto& type = checked_cast<const UnionType&>(*array.type());

      std::shared_ptr<Buffer> value_offsets;
      RETURN_NOT_OK(GetTruncatedBuffer<int32_t>(offset, length, array.value_offsets(),
                                                pool_, &value_offsets));

      // The Union type codes are not necessary 0-indexed
      uint8_t max_code = 0;
      for (uint8_t code : type.type_codes()) {
        if (code > max_code) {
          max_code = code;
        }
      }

      // Allocate an array of child offsets. Set all to -1 to indicate that we
      // haven't observed a first occurrence of a particular child yet
      std::vector<int32_t> child_offsets(max_code + 1, -1);
      std::vector<int32_t> child_lengths(max_code + 1, 0);

      if (offset != 0) {
        // This is an unpleasant case. Because the offsets are different for
        // each child array, when we have a sliced array, we need to "rebase"
        // the value_offsets for each array

        const int32_t* unshifted_offsets = array.raw_value_offsets();
        const uint8_t* type_ids = array.raw_type_ids();

        // Allocate the shifted offsets
        std::shared_ptr<Buffer> shifted_offsets_buffer;
        RETURN_NOT_OK(
            AllocateBuffer(pool_, length * sizeof(int32_t), &shifted_offsets_buffer));
        int32_t* shifted_offsets =
            reinterpret_cast<int32_t*>(shifted_offsets_buffer->mutable_data());

        // Offsets may not be ascending, so we need to find out the start offset
        // for each child
        for (int64_t i = 0; i < length; ++i) {
          const uint8_t code = type_ids[i];
          if (child_offsets[code] == -1) {
            child_offsets[code] = unshifted_offsets[i];
          } else {
            child_offsets[code] = std::min(child_offsets[code], unshifted_offsets[i]);
          }
        }

        // Now compute shifted offsets by subtracting child offset
        for (int64_t i = 0; i < length; ++i) {
          const uint8_t code = type_ids[i];
          shifted_offsets[i] = unshifted_offsets[i] - child_offsets[code];
          // Update the child length to account for observed value
          child_lengths[code] = std::max(child_lengths[code], shifted_offsets[i] + 1);
        }

        value_offsets = shifted_offsets_buffer;
      }
      out_->body_buffers.emplace_back(value_offsets);

      // Visit children and slice accordingly
      for (int i = 0; i < type.num_children(); ++i) {
        std::shared_ptr<Array> child = array.child(i);

        // TODO: ARROW-809, for sliced unions, tricky to know how much to
        // truncate the children. For now, we are truncating the children to be
        // no longer than the parent union.
        if (offset != 0) {
          const uint8_t code = type.type_codes()[i];
          const int64_t child_offset = child_offsets[code];
          const int64_t child_length = child_lengths[code];

          if (child_offset > 0) {
            child = child->Slice(child_offset, child_length);
          } else if (child_length < child->length()) {
            // This case includes when child is not encountered at all
            child = child->Slice(0, child_length);
          }
        }
        RETURN_NOT_OK(VisitArray(*child));
      }
    } else {
      for (int i = 0; i < array.num_fields(); ++i) {
        // Sparse union, slicing is done for us by child()
        RETURN_NOT_OK(VisitArray(*array.child(i)));
      }
    }
    ++max_recursion_depth_;
    return Status::OK();
  }

  Status Visit(const DictionaryArray& array) override {
    // Dictionary written out separately. Slice offset contained in the indices
    return array.indices()->Accept(this);
  }

  // Destination for output buffers
  IpcPayload* out_;

  // In some cases, intermediate buffers may need to be allocated (with sliced arrays)
  MemoryPool* pool_;

  std::vector<internal::FieldMetadata> field_nodes_;
  std::vector<internal::BufferMetadata> buffer_meta_;

  int64_t max_recursion_depth_;
  int64_t buffer_start_offset_;
  bool allow_64bit_;
};

class DictionaryWriter : public RecordBatchSerializer {
 public:
  DictionaryWriter(int64_t dictionary_id, MemoryPool* pool, int64_t buffer_start_offset,
                   int max_recursion_depth, bool allow_64bit, IpcPayload* out)
      : RecordBatchSerializer(pool, buffer_start_offset, max_recursion_depth, allow_64bit,
                              out),
        dictionary_id_(dictionary_id) {}

  Status SerializeMetadata(int64_t num_rows) override {
    return WriteDictionaryMessage(dictionary_id_, num_rows, out_->body_length,
                                  field_nodes_, buffer_meta_, &out_->metadata);
  }

  Status Assemble(const std::shared_ptr<Array>& dictionary) {
    // Make a dummy record batch. A bit tedious as we have to make a schema
    auto schema = arrow::schema({arrow::field("dictionary", dictionary->type())});
    auto batch = RecordBatch::Make(schema, dictionary->length(), {dictionary});
    return RecordBatchSerializer::Assemble(*batch);
  }

 private:
  int64_t dictionary_id_;
};

Status WriteIpcPayload(const IpcPayload& payload, io::OutputStream* dst,
                       int32_t* metadata_length) {
  RETURN_NOT_OK(internal::WriteMessage(*payload.metadata, kArrowIpcAlignment, dst,
                                       metadata_length));

#ifndef NDEBUG
  RETURN_NOT_OK(CheckAligned(dst));
#endif

  // Now write the buffers
  for (size_t i = 0; i < payload.body_buffers.size(); ++i) {
    const Buffer* buffer = payload.body_buffers[i].get();
    int64_t size = 0;
    int64_t padding = 0;

    // The buffer might be null if we are handling zero row lengths.
    if (buffer) {
      size = buffer->size();
      padding = BitUtil::RoundUpToMultipleOf8(size) - size;
    }

    if (size > 0) {
      RETURN_NOT_OK(dst->Write(buffer->data(), size));
    }

    if (padding > 0) {
      RETURN_NOT_OK(dst->Write(kPaddingBytes, padding));
    }
  }

#ifndef NDEBUG
  RETURN_NOT_OK(CheckAligned(dst));
#endif

  return Status::OK();
}

Status GetSchemaPayload(const Schema& schema, MemoryPool* pool,
                        DictionaryMemo* dictionary_memo, IpcPayload* out) {
  out->type = Message::Type::SCHEMA;
  out->body_buffers.clear();
  out->body_length = 0;
  RETURN_NOT_OK(SerializeSchema(schema, pool, &out->metadata));
  return WriteSchemaMessage(schema, dictionary_memo, &out->metadata);
}

Status GetRecordBatchPayload(const RecordBatch& batch, MemoryPool* pool,
                             IpcPayload* out) {
  RecordBatchSerializer writer(pool, 0, kMaxNestingDepth, true, out);
  return writer.Assemble(batch);
}

}  // namespace internal

Status WriteRecordBatch(const RecordBatch& batch, int64_t buffer_start_offset,
                        io::OutputStream* dst, int32_t* metadata_length,
                        int64_t* body_length, MemoryPool* pool, int max_recursion_depth,
                        bool allow_64bit) {
  internal::IpcPayload payload;
  internal::RecordBatchSerializer writer(pool, buffer_start_offset, max_recursion_depth,
                                         allow_64bit, &payload);
  RETURN_NOT_OK(writer.Assemble(batch));

  // TODO(wesm): it's a rough edge that the metadata and body length here are
  // computed separately

  // The body size is computed in the payload
  *body_length = payload.body_length;

  return internal::WriteIpcPayload(payload, dst, metadata_length);
}

Status WriteRecordBatchStream(const std::vector<std::shared_ptr<RecordBatch>>& batches,
                              io::OutputStream* dst) {
  std::shared_ptr<RecordBatchWriter> writer;
  RETURN_NOT_OK(RecordBatchStreamWriter::Open(dst, batches[0]->schema(), &writer));
  for (const auto& batch : batches) {
    // allow sizes > INT32_MAX
    DCHECK(batch->schema()->Equals(*batches[0]->schema())) << "Schemas unequal";
    RETURN_NOT_OK(writer->WriteRecordBatch(*batch, true));
  }
  RETURN_NOT_OK(writer->Close());
  return Status::OK();
}

Status WriteLargeRecordBatch(const RecordBatch& batch, int64_t buffer_start_offset,
                             io::OutputStream* dst, int32_t* metadata_length,
                             int64_t* body_length, MemoryPool* pool) {
  return WriteRecordBatch(batch, buffer_start_offset, dst, metadata_length, body_length,
                          pool, kMaxNestingDepth, true);
}

namespace {

Status WriteTensorHeader(const Tensor& tensor, io::OutputStream* dst,
                         int32_t* metadata_length) {
  std::shared_ptr<Buffer> metadata;
  RETURN_NOT_OK(internal::WriteTensorMessage(tensor, 0, &metadata));
  return internal::WriteMessage(*metadata, kTensorAlignment, dst, metadata_length);
}

Status WriteStridedTensorData(int dim_index, int64_t offset, int elem_size,
                              const Tensor& tensor, uint8_t* scratch_space,
                              io::OutputStream* dst) {
  if (dim_index == tensor.ndim() - 1) {
    const uint8_t* data_ptr = tensor.raw_data() + offset;
    const int64_t stride = tensor.strides()[dim_index];
    for (int64_t i = 0; i < tensor.shape()[dim_index]; ++i) {
      memcpy(scratch_space + i * elem_size, data_ptr, elem_size);
      data_ptr += stride;
    }
    return dst->Write(scratch_space, elem_size * tensor.shape()[dim_index]);
  }
  for (int64_t i = 0; i < tensor.shape()[dim_index]; ++i) {
    RETURN_NOT_OK(WriteStridedTensorData(dim_index + 1, offset, elem_size, tensor,
                                         scratch_space, dst));
    offset += tensor.strides()[dim_index];
  }
  return Status::OK();
}

Status GetContiguousTensor(const Tensor& tensor, MemoryPool* pool,
                           std::unique_ptr<Tensor>* out) {
  const auto& type = checked_cast<const FixedWidthType&>(*tensor.type());
  const int elem_size = type.bit_width() / 8;

  std::shared_ptr<Buffer> scratch_space;
  RETURN_NOT_OK(AllocateBuffer(pool, tensor.shape()[tensor.ndim() - 1] * elem_size,
                               &scratch_space));

  std::shared_ptr<ResizableBuffer> contiguous_data;
  RETURN_NOT_OK(
      AllocateResizableBuffer(pool, tensor.size() * elem_size, &contiguous_data));

  io::BufferOutputStream stream(contiguous_data);
  RETURN_NOT_OK(WriteStridedTensorData(0, 0, elem_size, tensor,
                                       scratch_space->mutable_data(), &stream));

  out->reset(new Tensor(tensor.type(), contiguous_data, tensor.shape()));

  return Status::OK();
}

}  // namespace

Status WriteTensor(const Tensor& tensor, io::OutputStream* dst, int32_t* metadata_length,
                   int64_t* body_length) {
  const auto& type = checked_cast<const FixedWidthType&>(*tensor.type());
  const int elem_size = type.bit_width() / 8;

  *body_length = tensor.size() * elem_size;

  // Tensor metadata accounts for padding
  if (tensor.is_contiguous()) {
    RETURN_NOT_OK(WriteTensorHeader(tensor, dst, metadata_length));
    auto data = tensor.data();
    if (data && data->data()) {
      RETURN_NOT_OK(dst->Write(data->data(), *body_length));
    } else {
      *body_length = 0;
    }
  } else {
    // The tensor written is made contiguous
    Tensor dummy(tensor.type(), nullptr, tensor.shape());
    RETURN_NOT_OK(WriteTensorHeader(dummy, dst, metadata_length));

    // TODO(wesm): Do we care enough about this temporary allocation to pass in
    // a MemoryPool to this function?
    std::shared_ptr<Buffer> scratch_space;
    RETURN_NOT_OK(
        AllocateBuffer(tensor.shape()[tensor.ndim() - 1] * elem_size, &scratch_space));

    RETURN_NOT_OK(WriteStridedTensorData(0, 0, elem_size, tensor,
                                         scratch_space->mutable_data(), dst));
  }

  return Status::OK();
}

Status GetTensorMessage(const Tensor& tensor, MemoryPool* pool,
                        std::unique_ptr<Message>* out) {
  const Tensor* tensor_to_write = &tensor;
  std::unique_ptr<Tensor> temp_tensor;

  if (!tensor.is_contiguous()) {
    RETURN_NOT_OK(GetContiguousTensor(tensor, pool, &temp_tensor));
    tensor_to_write = temp_tensor.get();
  }

  std::shared_ptr<Buffer> metadata;
  RETURN_NOT_OK(internal::WriteTensorMessage(*tensor_to_write, 0, &metadata));
  out->reset(new Message(metadata, tensor_to_write->data()));
  return Status::OK();
}

namespace internal {

class SparseTensorSerializer {
 public:
  SparseTensorSerializer(int64_t buffer_start_offset, IpcPayload* out)
      : out_(out), buffer_start_offset_(buffer_start_offset) {}

  ~SparseTensorSerializer() = default;

  Status VisitSparseIndex(const SparseIndex& sparse_index) {
    switch (sparse_index.format_id()) {
      case SparseTensorFormat::COO:
        RETURN_NOT_OK(
            VisitSparseCOOIndex(checked_cast<const SparseCOOIndex&>(sparse_index)));
        break;

      case SparseTensorFormat::CSR:
        RETURN_NOT_OK(
            VisitSparseCSRIndex(checked_cast<const SparseCSRIndex&>(sparse_index)));
        break;

      default:
        std::stringstream ss;
        ss << "Unable to convert type: " << sparse_index.ToString() << std::endl;
        return Status::NotImplemented(ss.str());
    }

    return Status::OK();
  }

  Status SerializeMetadata(const SparseTensor& sparse_tensor) {
    return WriteSparseTensorMessage(sparse_tensor, out_->body_length, buffer_meta_,
                                    &out_->metadata);
  }

  Status Assemble(const SparseTensor& sparse_tensor) {
    if (buffer_meta_.size() > 0) {
      buffer_meta_.clear();
      out_->body_buffers.clear();
    }

    RETURN_NOT_OK(VisitSparseIndex(*sparse_tensor.sparse_index()));
    out_->body_buffers.emplace_back(sparse_tensor.data());

    int64_t offset = buffer_start_offset_;
    buffer_meta_.reserve(out_->body_buffers.size());

    for (size_t i = 0; i < out_->body_buffers.size(); ++i) {
      const Buffer* buffer = out_->body_buffers[i].get();
      int64_t size = buffer->size();
      int64_t padding = BitUtil::RoundUpToMultipleOf8(size) - size;
      buffer_meta_.push_back({offset, size + padding});
      offset += size + padding;
    }

    out_->body_length = offset - buffer_start_offset_;
    DCHECK(BitUtil::IsMultipleOf8(out_->body_length));

    return SerializeMetadata(sparse_tensor);
  }

 private:
  Status VisitSparseCOOIndex(const SparseCOOIndex& sparse_index) {
    out_->body_buffers.emplace_back(sparse_index.indices()->data());
    return Status::OK();
  }

  Status VisitSparseCSRIndex(const SparseCSRIndex& sparse_index) {
    out_->body_buffers.emplace_back(sparse_index.indptr()->data());
    out_->body_buffers.emplace_back(sparse_index.indices()->data());
    return Status::OK();
  }

  IpcPayload* out_;

  std::vector<internal::BufferMetadata> buffer_meta_;

  int64_t buffer_start_offset_;
};

Status GetSparseTensorPayload(const SparseTensor& sparse_tensor, MemoryPool* pool,
                              IpcPayload* out) {
  SparseTensorSerializer writer(0, out);
  return writer.Assemble(sparse_tensor);
}

}  // namespace internal

Status WriteSparseTensor(const SparseTensor& sparse_tensor, io::OutputStream* dst,
                         int32_t* metadata_length, int64_t* body_length,
                         MemoryPool* pool) {
  internal::IpcPayload payload;
  internal::SparseTensorSerializer writer(0, &payload);
  RETURN_NOT_OK(writer.Assemble(sparse_tensor));

  *body_length = payload.body_length;
  return internal::WriteIpcPayload(payload, dst, metadata_length);
}

Status WriteDictionary(int64_t dictionary_id, const std::shared_ptr<Array>& dictionary,
                       int64_t buffer_start_offset, io::OutputStream* dst,
                       int32_t* metadata_length, int64_t* body_length, MemoryPool* pool) {
  internal::IpcPayload payload;
  internal::DictionaryWriter writer(dictionary_id, pool, buffer_start_offset,
                                    kMaxNestingDepth, true, &payload);
  RETURN_NOT_OK(writer.Assemble(dictionary));

  // The body size is computed in the payload
  *body_length = payload.body_length;
  return internal::WriteIpcPayload(payload, dst, metadata_length);
}

Status GetRecordBatchSize(const RecordBatch& batch, int64_t* size) {
  // emulates the behavior of Write without actually writing
  int32_t metadata_length = 0;
  int64_t body_length = 0;
  io::MockOutputStream dst;
  RETURN_NOT_OK(WriteRecordBatch(batch, 0, &dst, &metadata_length, &body_length,
                                 default_memory_pool(), kMaxNestingDepth, true));
  *size = dst.GetExtentBytesWritten();
  return Status::OK();
}

Status GetTensorSize(const Tensor& tensor, int64_t* size) {
  // emulates the behavior of Write without actually writing
  int32_t metadata_length = 0;
  int64_t body_length = 0;
  io::MockOutputStream dst;
  RETURN_NOT_OK(WriteTensor(tensor, &dst, &metadata_length, &body_length));
  *size = dst.GetExtentBytesWritten();
  return Status::OK();
}

// ----------------------------------------------------------------------

RecordBatchWriter::~RecordBatchWriter() {}

Status RecordBatchWriter::WriteTable(const Table& table, int64_t max_chunksize) {
  TableBatchReader reader(table);

  if (max_chunksize > 0) {
    reader.set_chunksize(max_chunksize);
  }

  std::shared_ptr<RecordBatch> batch;
  while (true) {
    RETURN_NOT_OK(reader.ReadNext(&batch));
    if (batch == nullptr) {
      break;
    }
    RETURN_NOT_OK(WriteRecordBatch(*batch, true));
  }

  return Status::OK();
}

Status RecordBatchWriter::WriteTable(const Table& table) { return WriteTable(table, -1); }

// ----------------------------------------------------------------------
// Stream writer implementation

class StreamBookKeeper {
 public:
  StreamBookKeeper() : sink_(nullptr), position_(-1) {}
  explicit StreamBookKeeper(io::OutputStream* sink) : sink_(sink), position_(-1) {}

  Status UpdatePosition() { return sink_->Tell(&position_); }

  Status UpdatePositionCheckAligned() {
    RETURN_NOT_OK(UpdatePosition());
    DCHECK_EQ(0, position_ % 8) << "Stream is not aligned";
    return Status::OK();
  }

  Status Align(int32_t alignment = kArrowIpcAlignment) {
    // Adds padding bytes if necessary to ensure all memory blocks are written on
    // 8-byte (or other alignment) boundaries.
    int64_t remainder = PaddedLength(position_, alignment) - position_;
    if (remainder > 0) {
      return Write(kPaddingBytes, remainder);
    }
    return Status::OK();
  }

  // Write data and update position
  Status Write(const void* data, int64_t nbytes) {
    RETURN_NOT_OK(sink_->Write(data, nbytes));
    position_ += nbytes;
    return Status::OK();
  }

 protected:
  io::OutputStream* sink_;
  int64_t position_;
};

class SchemaWriter : public StreamBookKeeper {
 public:
  SchemaWriter(const Schema& schema, DictionaryMemo* dictionary_memo, MemoryPool* pool,
               io::OutputStream* sink)
      : StreamBookKeeper(sink),
        pool_(pool),
        schema_(schema),
        dictionary_memo_(dictionary_memo) {}

  Status WriteSchema() {
#ifndef NDEBUG
    // Catch bug fixed in ARROW-3236
    RETURN_NOT_OK(UpdatePositionCheckAligned());
#endif

    std::shared_ptr<Buffer> schema_fb;
    RETURN_NOT_OK(internal::WriteSchemaMessage(schema_, dictionary_memo_, &schema_fb));

    int32_t metadata_length = 0;
    RETURN_NOT_OK(internal::WriteMessage(*schema_fb, 8, sink_, &metadata_length));
    RETURN_NOT_OK(UpdatePositionCheckAligned());
    return Status::OK();
  }

  Status WriteDictionaries(std::vector<FileBlock>* dictionaries) {
    const DictionaryMap& id_to_dictionary = dictionary_memo_->id_to_dictionary();

    dictionaries->resize(id_to_dictionary.size());

    // TODO(wesm): does sorting by id yield any benefit?
    int dict_index = 0;
    for (const auto& entry : id_to_dictionary) {
      FileBlock* block = &(*dictionaries)[dict_index++];

      block->offset = position_;

      // Frame of reference in file format is 0, see ARROW-384
      const int64_t buffer_start_offset = 0;
      RETURN_NOT_OK(WriteDictionary(entry.first, entry.second, buffer_start_offset, sink_,
                                    &block->metadata_length, &block->body_length, pool_));
      RETURN_NOT_OK(UpdatePositionCheckAligned());
    }

    return Status::OK();
  }

  Status Write(std::vector<FileBlock>* dictionaries) {
    RETURN_NOT_OK(WriteSchema());

    // If there are any dictionaries, write them as the next messages
    return WriteDictionaries(dictionaries);
  }

 private:
  MemoryPool* pool_;
  const Schema& schema_;
  DictionaryMemo* dictionary_memo_;
};

class RecordBatchStreamWriter::RecordBatchStreamWriterImpl : public StreamBookKeeper {
 public:
  RecordBatchStreamWriterImpl(io::OutputStream* sink,
                              const std::shared_ptr<Schema>& schema)
      : StreamBookKeeper(sink),
        schema_(schema),
        pool_(default_memory_pool()),
        started_(false) {}

  virtual ~RecordBatchStreamWriterImpl() = default;

  virtual Status Start() {
    SchemaWriter schema_writer(*schema_, &dictionary_memo_, pool_, sink_);
    RETURN_NOT_OK(schema_writer.Write(&dictionaries_));
    started_ = true;
    return Status::OK();
  }

  virtual Status Close() {
    // Write the schema if not already written
    // User is responsible for closing the OutputStream
    RETURN_NOT_OK(CheckStarted());

    // Write 0 EOS message
    const int32_t kEos = 0;
    return Write(&kEos, sizeof(int32_t));
  }

  Status CheckStarted() {
    if (!started_) {
      return Start();
    }
    return Status::OK();
  }

  Status WriteRecordBatch(const RecordBatch& batch, bool allow_64bit, FileBlock* block) {
    RETURN_NOT_OK(CheckStarted());
    RETURN_NOT_OK(UpdatePosition());

    block->offset = position_;

    // Frame of reference in file format is 0, see ARROW-384
    const int64_t buffer_start_offset = 0;
    RETURN_NOT_OK(arrow::ipc::WriteRecordBatch(
        batch, buffer_start_offset, sink_, &block->metadata_length, &block->body_length,
        pool_, kMaxNestingDepth, allow_64bit));
    RETURN_NOT_OK(UpdatePositionCheckAligned());

    return Status::OK();
  }

  Status WriteRecordBatch(const RecordBatch& batch, bool allow_64bit) {
    // Push an empty FileBlock. Can be written in the footer later
    record_batches_.push_back({0, 0, 0});
    return WriteRecordBatch(batch, allow_64bit,
                            &record_batches_[record_batches_.size() - 1]);
  }

  void set_memory_pool(MemoryPool* pool) { pool_ = pool; }

 protected:
  std::shared_ptr<Schema> schema_;
  MemoryPool* pool_;
  bool started_;

  // When writing out the schema, we keep track of all the dictionaries we
  // encounter, as they must be written out first in the stream
  DictionaryMemo dictionary_memo_;

  std::vector<FileBlock> dictionaries_;
  std::vector<FileBlock> record_batches_;
};

RecordBatchStreamWriter::RecordBatchStreamWriter() {}

RecordBatchStreamWriter::~RecordBatchStreamWriter() {}

Status RecordBatchStreamWriter::WriteRecordBatch(const RecordBatch& batch,
                                                 bool allow_64bit) {
  return impl_->WriteRecordBatch(batch, allow_64bit);
}

void RecordBatchStreamWriter::set_memory_pool(MemoryPool* pool) {
  impl_->set_memory_pool(pool);
}

Status RecordBatchStreamWriter::Open(io::OutputStream* sink,
                                     const std::shared_ptr<Schema>& schema,
                                     std::shared_ptr<RecordBatchWriter>* out) {
  // ctor is private
  auto result = std::shared_ptr<RecordBatchStreamWriter>(new RecordBatchStreamWriter());
  result->impl_.reset(new RecordBatchStreamWriterImpl(sink, schema));
  *out = result;
  return Status::OK();
}

Status RecordBatchStreamWriter::Close() { return impl_->Close(); }

// ----------------------------------------------------------------------
// File writer implementation

class RecordBatchFileWriter::RecordBatchFileWriterImpl
    : public RecordBatchStreamWriter::RecordBatchStreamWriterImpl {
 public:
  using BASE = RecordBatchStreamWriter::RecordBatchStreamWriterImpl;

  RecordBatchFileWriterImpl(io::OutputStream* sink, const std::shared_ptr<Schema>& schema)
      : BASE(sink, schema) {}

  Status Start() override {
    // ARROW-3236: The initial position -1 needs to be updated to the stream's
    // current position otherwise an incorrect amount of padding will be
    // written to new files.
    RETURN_NOT_OK(UpdatePosition());

    // It is only necessary to align to 8-byte boundary at the start of the file
    RETURN_NOT_OK(Write(kArrowMagicBytes, strlen(kArrowMagicBytes)));
    RETURN_NOT_OK(Align());

    // We write the schema at the start of the file (and the end). This also
    // writes all the dictionaries at the beginning of the file
    return BASE::Start();
  }

  Status Close() override {
    // Write the schema if not already written
    // User is responsible for closing the OutputStream
    RETURN_NOT_OK(CheckStarted());

    // Write metadata
    RETURN_NOT_OK(UpdatePosition());

    int64_t initial_position = position_;
    RETURN_NOT_OK(WriteFileFooter(*schema_, dictionaries_, record_batches_,
                                  &dictionary_memo_, sink_));
    RETURN_NOT_OK(UpdatePosition());

    // Write footer length
    int32_t footer_length = static_cast<int32_t>(position_ - initial_position);

    if (footer_length <= 0) {
      return Status::Invalid("Invalid file footer");
    }

    RETURN_NOT_OK(Write(&footer_length, sizeof(int32_t)));

    // Write magic bytes to end file
    return Write(kArrowMagicBytes, strlen(kArrowMagicBytes));
  }
};

RecordBatchFileWriter::RecordBatchFileWriter() {}

RecordBatchFileWriter::~RecordBatchFileWriter() {}

Status RecordBatchFileWriter::Open(io::OutputStream* sink,
                                   const std::shared_ptr<Schema>& schema,
                                   std::shared_ptr<RecordBatchWriter>* out) {
  // ctor is private
  auto result = std::shared_ptr<RecordBatchFileWriter>(new RecordBatchFileWriter());
  result->file_impl_.reset(new RecordBatchFileWriterImpl(sink, schema));
  *out = result;
  return Status::OK();
}

Status RecordBatchFileWriter::WriteRecordBatch(const RecordBatch& batch,
                                               bool allow_64bit) {
  return file_impl_->WriteRecordBatch(batch, allow_64bit);
}

Status RecordBatchFileWriter::Close() { return file_impl_->Close(); }

// ----------------------------------------------------------------------
// Serialization public APIs

Status SerializeRecordBatch(const RecordBatch& batch, MemoryPool* pool,
                            std::shared_ptr<Buffer>* out) {
  int64_t size = 0;
  RETURN_NOT_OK(GetRecordBatchSize(batch, &size));
  std::shared_ptr<Buffer> buffer;
  RETURN_NOT_OK(AllocateBuffer(pool, size, &buffer));

  io::FixedSizeBufferWriter stream(buffer);
  RETURN_NOT_OK(SerializeRecordBatch(batch, pool, &stream));
  *out = buffer;
  return Status::OK();
}

Status SerializeRecordBatch(const RecordBatch& batch, MemoryPool* pool,
                            io::OutputStream* out) {
  int32_t metadata_length = 0;
  int64_t body_length = 0;
  return WriteRecordBatch(batch, 0, out, &metadata_length, &body_length, pool,
                          kMaxNestingDepth, true);
}

Status SerializeSchema(const Schema& schema, MemoryPool* pool,
                       std::shared_ptr<Buffer>* out) {
  std::shared_ptr<io::BufferOutputStream> stream;
  RETURN_NOT_OK(io::BufferOutputStream::Create(1024, pool, &stream));

  DictionaryMemo memo;
  SchemaWriter schema_writer(schema, &memo, pool, stream.get());

  // Unused
  std::vector<FileBlock> dictionary_blocks;

  RETURN_NOT_OK(schema_writer.Write(&dictionary_blocks));
  return stream->Finish(out);
}

}  // namespace ipc
}  // namespace arrow
