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

#include "arrow/adapters/orc/adapter.h"

#include <algorithm>
#include <cstdint>
#include <list>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "arrow/buffer.h"
#include "arrow/builder.h"
#include "arrow/io/interfaces.h"
#include "arrow/memory_pool.h"
#include "arrow/record_batch.h"
#include "arrow/status.h"
#include "arrow/table.h"
#include "arrow/table_builder.h"
#include "arrow/type.h"
#include "arrow/type_traits.h"
#include "arrow/util/bit-util.h"
#include "arrow/util/checked_cast.h"
#include "arrow/util/decimal.h"
#include "arrow/util/lazy.h"
#include "arrow/util/macros.h"
#include "arrow/util/visibility.h"

#include "orc/Exceptions.hh"
#include "orc/OrcFile.hh"

// alias to not interfere with nested orc namespace
namespace liborc = orc;

namespace arrow {

using internal::checked_cast;

namespace adapters {
namespace orc {

#define ORC_THROW_NOT_OK(s)                   \
  do {                                        \
    Status _s = (s);                          \
    if (!_s.ok()) {                           \
      std::stringstream ss;                   \
      ss << "Arrow error: " << _s.ToString(); \
      throw liborc::ParseError(ss.str());     \
    }                                         \
  } while (0)

class ArrowInputFile : public liborc::InputStream {
 public:
  explicit ArrowInputFile(const std::shared_ptr<io::ReadableFileInterface>& file)
      : file_(file) {}

  uint64_t getLength() const override {
    int64_t size;
    ORC_THROW_NOT_OK(file_->GetSize(&size));
    return static_cast<uint64_t>(size);
  }

  uint64_t getNaturalReadSize() const override { return 128 * 1024; }

  void read(void* buf, uint64_t length, uint64_t offset) override {
    int64_t bytes_read;

    ORC_THROW_NOT_OK(file_->ReadAt(offset, length, &bytes_read, buf));

    if (static_cast<uint64_t>(bytes_read) != length) {
      throw liborc::ParseError("Short read from arrow input file");
    }
  }

  const std::string& getName() const override {
    static const std::string filename("ArrowInputFile");
    return filename;
  }

 private:
  std::shared_ptr<io::ReadableFileInterface> file_;
};

struct StripeInformation {
  uint64_t offset;
  uint64_t length;
  uint64_t num_rows;
};

Status GetArrowType(const liborc::Type* type, std::shared_ptr<DataType>* out) {
  // When subselecting fields on read, liborc will set some nodes to nullptr,
  // so we need to check for nullptr before progressing
  if (type == nullptr) {
    *out = null();
    return Status::OK();
  }
  liborc::TypeKind kind = type->getKind();
  const int subtype_count = static_cast<int>(type->getSubtypeCount());

  switch (kind) {
    case liborc::BOOLEAN:
      *out = boolean();
      break;
    case liborc::BYTE:
      *out = int8();
      break;
    case liborc::SHORT:
      *out = int16();
      break;
    case liborc::INT:
      *out = int32();
      break;
    case liborc::LONG:
      *out = int64();
      break;
    case liborc::FLOAT:
      *out = float32();
      break;
    case liborc::DOUBLE:
      *out = float64();
      break;
    case liborc::VARCHAR:
    case liborc::STRING:
      *out = utf8();
      break;
    case liborc::BINARY:
      *out = binary();
      break;
    case liborc::CHAR:
      *out = fixed_size_binary(static_cast<int>(type->getMaximumLength()));
      break;
    case liborc::TIMESTAMP:
      *out = timestamp(TimeUnit::NANO);
      break;
    case liborc::DATE:
      *out = date32();
      break;
    case liborc::DECIMAL: {
      const int precision = static_cast<int>(type->getPrecision());
      const int scale = static_cast<int>(type->getScale());
      if (precision == 0) {
        // In HIVE 0.11/0.12 precision is set as 0, but means max precision
        *out = decimal(38, 6);
      } else {
        *out = decimal(precision, scale);
      }
      break;
    }
    case liborc::LIST: {
      if (subtype_count != 1) {
        return Status::Invalid("Invalid Orc List type");
      }
      std::shared_ptr<DataType> elemtype;
      RETURN_NOT_OK(GetArrowType(type->getSubtype(0), &elemtype));
      *out = list(elemtype);
      break;
    }
    case liborc::MAP: {
      if (subtype_count != 2) {
        return Status::Invalid("Invalid Orc Map type");
      }
      std::shared_ptr<DataType> keytype;
      std::shared_ptr<DataType> valtype;
      RETURN_NOT_OK(GetArrowType(type->getSubtype(0), &keytype));
      RETURN_NOT_OK(GetArrowType(type->getSubtype(1), &valtype));
      *out = list(struct_({field("key", keytype), field("value", valtype)}));
      break;
    }
    case liborc::STRUCT: {
      std::vector<std::shared_ptr<Field>> fields;
      for (int child = 0; child < subtype_count; ++child) {
        std::shared_ptr<DataType> elemtype;
        RETURN_NOT_OK(GetArrowType(type->getSubtype(child), &elemtype));
        std::string name = type->getFieldName(child);
        fields.push_back(field(name, elemtype));
      }
      *out = struct_(fields);
      break;
    }
    case liborc::UNION: {
      std::vector<std::shared_ptr<Field>> fields;
      std::vector<uint8_t> type_codes;
      for (int child = 0; child < subtype_count; ++child) {
        std::shared_ptr<DataType> elemtype;
        RETURN_NOT_OK(GetArrowType(type->getSubtype(child), &elemtype));
        fields.push_back(field("_union_" + std::to_string(child), elemtype));
        type_codes.push_back(static_cast<uint8_t>(child));
      }
      *out = union_(fields, type_codes);
      break;
    }
    default: { return Status::Invalid("Unknown Orc type kind: ", kind); }
  }
  return Status::OK();
}

// The number of rows to read in a ColumnVectorBatch
constexpr int64_t kReadRowsBatch = 1000;

// The numer of nanoseconds in a second
constexpr int64_t kOneSecondNanos = 1000000000LL;

class ORCFileReader::Impl {
 public:
  Impl() {}
  ~Impl() {}

  Status Open(const std::shared_ptr<io::ReadableFileInterface>& file, MemoryPool* pool) {
    std::unique_ptr<ArrowInputFile> io_wrapper(new ArrowInputFile(file));
    liborc::ReaderOptions options;
    std::unique_ptr<liborc::Reader> liborc_reader;
    try {
      liborc_reader = createReader(std::move(io_wrapper), options);
    } catch (const liborc::ParseError& e) {
      return Status::IOError(e.what());
    }
    pool_ = pool;
    reader_ = std::move(liborc_reader);

    return Init();
  }

  Status Init() {
    int64_t nstripes = reader_->getNumberOfStripes();
    stripes_.resize(nstripes);
    std::unique_ptr<liborc::StripeInformation> stripe;
    for (int i = 0; i < nstripes; ++i) {
      stripe = reader_->getStripe(i);
      stripes_[i] = StripeInformation(
          {stripe->getOffset(), stripe->getLength(), stripe->getNumberOfRows()});
    }
    return Status::OK();
  }

  int64_t NumberOfStripes() { return stripes_.size(); }

  int64_t NumberOfRows() { return reader_->getNumberOfRows(); }

  Status ReadSchema(std::shared_ptr<Schema>* out) {
    const liborc::Type& type = reader_->getType();
    return GetArrowSchema(type, out);
  }

  Status ReadSchema(const liborc::RowReaderOptions& opts, std::shared_ptr<Schema>* out) {
    std::unique_ptr<liborc::RowReader> row_reader;
    try {
      row_reader = reader_->createRowReader(opts);
    } catch (const liborc::ParseError& e) {
      return Status::Invalid(e.what());
    }
    const liborc::Type& type = row_reader->getSelectedType();
    return GetArrowSchema(type, out);
  }

  Status GetArrowSchema(const liborc::Type& type, std::shared_ptr<Schema>* out) {
    if (type.getKind() != liborc::STRUCT) {
      return Status::NotImplemented(
          "Only ORC files with a top-level struct "
          "can be handled");
    }
    int size = static_cast<int>(type.getSubtypeCount());
    std::vector<std::shared_ptr<Field>> fields;
    for (int child = 0; child < size; ++child) {
      std::shared_ptr<DataType> elemtype;
      RETURN_NOT_OK(GetArrowType(type.getSubtype(child), &elemtype));
      std::string name = type.getFieldName(child);
      fields.push_back(field(name, elemtype));
    }
    std::list<std::string> keys = reader_->getMetadataKeys();
    std::shared_ptr<KeyValueMetadata> metadata;
    if (!keys.empty()) {
      metadata = std::make_shared<KeyValueMetadata>();
      for (auto it = keys.begin(); it != keys.end(); ++it) {
        metadata->Append(*it, reader_->getMetadataValue(*it));
      }
    }

    *out = std::make_shared<Schema>(fields, metadata);
    return Status::OK();
  }

  Status Read(std::shared_ptr<Table>* out) {
    liborc::RowReaderOptions opts;
    std::shared_ptr<Schema> schema;
    RETURN_NOT_OK(ReadSchema(opts, &schema));
    return ReadTable(opts, schema, out);
  }

  Status Read(const std::shared_ptr<Schema>& schema, std::shared_ptr<Table>* out) {
    liborc::RowReaderOptions opts;
    return ReadTable(opts, schema, out);
  }

  Status Read(const std::vector<int>& include_indices, std::shared_ptr<Table>* out) {
    liborc::RowReaderOptions opts;
    RETURN_NOT_OK(SelectIndices(&opts, include_indices));
    std::shared_ptr<Schema> schema;
    RETURN_NOT_OK(ReadSchema(opts, &schema));
    return ReadTable(opts, schema, out);
  }

  Status Read(const std::shared_ptr<Schema>& schema,
              const std::vector<int>& include_indices, std::shared_ptr<Table>* out) {
    liborc::RowReaderOptions opts;
    RETURN_NOT_OK(SelectIndices(&opts, include_indices));
    return ReadTable(opts, schema, out);
  }

  Status ReadStripe(int64_t stripe, std::shared_ptr<RecordBatch>* out) {
    liborc::RowReaderOptions opts;
    RETURN_NOT_OK(SelectStripe(&opts, stripe));
    std::shared_ptr<Schema> schema;
    RETURN_NOT_OK(ReadSchema(opts, &schema));
    return ReadBatch(opts, schema, stripes_[stripe].num_rows, out);
  }

  Status ReadStripe(int64_t stripe, const std::vector<int>& include_indices,
                    std::shared_ptr<RecordBatch>* out) {
    liborc::RowReaderOptions opts;
    RETURN_NOT_OK(SelectIndices(&opts, include_indices));
    RETURN_NOT_OK(SelectStripe(&opts, stripe));
    std::shared_ptr<Schema> schema;
    RETURN_NOT_OK(ReadSchema(opts, &schema));
    return ReadBatch(opts, schema, stripes_[stripe].num_rows, out);
  }

  Status SelectStripe(liborc::RowReaderOptions* opts, int64_t stripe) {
    ARROW_RETURN_IF(stripe < 0 || stripe >= NumberOfStripes(),
                    Status::Invalid("Out of bounds stripe: ", stripe));

    opts->range(stripes_[stripe].offset, stripes_[stripe].length);
    return Status::OK();
  }

  Status SelectIndices(liborc::RowReaderOptions* opts,
                       const std::vector<int>& include_indices) {
    std::list<uint64_t> include_indices_list;
    for (auto it = include_indices.begin(); it != include_indices.end(); ++it) {
      ARROW_RETURN_IF(*it < 0, Status::Invalid("Negative field index"));
      include_indices_list.push_back(*it);
    }
    opts->includeTypes(include_indices_list);
    return Status::OK();
  }

  Status ReadTable(const liborc::RowReaderOptions& row_opts,
                   const std::shared_ptr<Schema>& schema, std::shared_ptr<Table>* out) {
    liborc::RowReaderOptions opts(row_opts);
    std::vector<std::shared_ptr<RecordBatch>> batches(stripes_.size());
    for (size_t stripe = 0; stripe < stripes_.size(); stripe++) {
      opts.range(stripes_[stripe].offset, stripes_[stripe].length);
      RETURN_NOT_OK(ReadBatch(opts, schema, stripes_[stripe].num_rows, &batches[stripe]));
    }
    return Table::FromRecordBatches(schema, batches, out);
  }

  Status ReadBatch(const liborc::RowReaderOptions& opts,
                   const std::shared_ptr<Schema>& schema, int64_t nrows,
                   std::shared_ptr<RecordBatch>* out) {
    std::unique_ptr<liborc::RowReader> rowreader;
    std::unique_ptr<liborc::ColumnVectorBatch> batch;
    try {
      rowreader = reader_->createRowReader(opts);
      batch = rowreader->createRowBatch(std::min(nrows, kReadRowsBatch));
    } catch (const liborc::ParseError& e) {
      return Status::Invalid(e.what());
    }
    std::unique_ptr<RecordBatchBuilder> builder;
    RETURN_NOT_OK(RecordBatchBuilder::Make(schema, pool_, nrows, &builder));

    // The top-level type must be a struct to read into an arrow table
    const auto& struct_batch = checked_cast<liborc::StructVectorBatch&>(*batch);

    const liborc::Type& type = rowreader->getSelectedType();
    while (rowreader->next(*batch)) {
      for (int i = 0; i < builder->num_fields(); i++) {
        RETURN_NOT_OK(AppendBatch(type.getSubtype(i), struct_batch.fields[i], 0,
                                  batch->numElements, builder->GetField(i)));
      }
    }
    RETURN_NOT_OK(builder->Flush(out));
    return Status::OK();
  }

  Status AppendBatch(const liborc::Type* type, liborc::ColumnVectorBatch* batch,
                     int64_t offset, int64_t length, ArrayBuilder* builder) {
    if (type == nullptr) {
      return Status::OK();
    }
    liborc::TypeKind kind = type->getKind();
    switch (kind) {
      case liborc::STRUCT:
        return AppendStructBatch(type, batch, offset, length, builder);
      case liborc::LIST:
        return AppendListBatch(type, batch, offset, length, builder);
      case liborc::MAP:
        return AppendMapBatch(type, batch, offset, length, builder);
      case liborc::LONG:
        return AppendNumericBatch<Int64Builder, liborc::LongVectorBatch, int64_t>(
            batch, offset, length, builder);
      case liborc::INT:
        return AppendNumericBatchCast<Int32Builder, int32_t, liborc::LongVectorBatch,
                                      int64_t>(batch, offset, length, builder);
      case liborc::SHORT:
        return AppendNumericBatchCast<Int16Builder, int16_t, liborc::LongVectorBatch,
                                      int64_t>(batch, offset, length, builder);
      case liborc::BYTE:
        return AppendNumericBatchCast<Int8Builder, int8_t, liborc::LongVectorBatch,
                                      int64_t>(batch, offset, length, builder);
      case liborc::DOUBLE:
        return AppendNumericBatch<DoubleBuilder, liborc::DoubleVectorBatch, double>(
            batch, offset, length, builder);
      case liborc::FLOAT:
        return AppendNumericBatchCast<FloatBuilder, float, liborc::DoubleVectorBatch,
                                      double>(batch, offset, length, builder);
      case liborc::BOOLEAN:
        return AppendBoolBatch(batch, offset, length, builder);
      case liborc::VARCHAR:
      case liborc::STRING:
        return AppendBinaryBatch<StringBuilder>(batch, offset, length, builder);
      case liborc::BINARY:
        return AppendBinaryBatch<BinaryBuilder>(batch, offset, length, builder);
      case liborc::CHAR:
        return AppendFixedBinaryBatch(batch, offset, length, builder);
      case liborc::DATE:
        return AppendNumericBatchCast<Date32Builder, int32_t, liborc::LongVectorBatch,
                                      int64_t>(batch, offset, length, builder);
      case liborc::TIMESTAMP:
        return AppendTimestampBatch(batch, offset, length, builder);
      case liborc::DECIMAL:
        return AppendDecimalBatch(type, batch, offset, length, builder);
      default:
        return Status::NotImplemented("Not implemented type kind: ", kind);
    }
  }

  Status AppendStructBatch(const liborc::Type* type, liborc::ColumnVectorBatch* cbatch,
                           int64_t offset, int64_t length, ArrayBuilder* abuilder) {
    auto builder = checked_cast<StructBuilder*>(abuilder);
    auto batch = checked_cast<liborc::StructVectorBatch*>(cbatch);

    const uint8_t* valid_bytes = nullptr;
    if (batch->hasNulls) {
      valid_bytes = reinterpret_cast<const uint8_t*>(batch->notNull.data()) + offset;
    }
    RETURN_NOT_OK(builder->AppendValues(length, valid_bytes));

    for (int i = 0; i < builder->num_fields(); i++) {
      RETURN_NOT_OK(AppendBatch(type->getSubtype(i), batch->fields[i], offset, length,
                                builder->field_builder(i)));
    }
    return Status::OK();
  }

  Status AppendListBatch(const liborc::Type* type, liborc::ColumnVectorBatch* cbatch,
                         int64_t offset, int64_t length, ArrayBuilder* abuilder) {
    auto builder = checked_cast<ListBuilder*>(abuilder);
    auto batch = checked_cast<liborc::ListVectorBatch*>(cbatch);
    liborc::ColumnVectorBatch* elements = batch->elements.get();
    const liborc::Type* elemtype = type->getSubtype(0);

    const bool has_nulls = batch->hasNulls;
    for (int64_t i = offset; i < length + offset; i++) {
      if (!has_nulls || batch->notNull[i]) {
        int64_t start = batch->offsets[i];
        int64_t end = batch->offsets[i + 1];
        RETURN_NOT_OK(builder->Append());
        RETURN_NOT_OK(AppendBatch(elemtype, elements, start, end - start,
                                  builder->value_builder()));
      } else {
        RETURN_NOT_OK(builder->AppendNull());
      }
    }
    return Status::OK();
  }

  Status AppendMapBatch(const liborc::Type* type, liborc::ColumnVectorBatch* cbatch,
                        int64_t offset, int64_t length, ArrayBuilder* abuilder) {
    auto list_builder = checked_cast<ListBuilder*>(abuilder);
    auto struct_builder = checked_cast<StructBuilder*>(list_builder->value_builder());
    auto batch = checked_cast<liborc::MapVectorBatch*>(cbatch);
    liborc::ColumnVectorBatch* keys = batch->keys.get();
    liborc::ColumnVectorBatch* vals = batch->elements.get();
    const liborc::Type* keytype = type->getSubtype(0);
    const liborc::Type* valtype = type->getSubtype(1);

    const bool has_nulls = batch->hasNulls;
    for (int64_t i = offset; i < length + offset; i++) {
      RETURN_NOT_OK(list_builder->Append());
      int64_t start = batch->offsets[i];
      int64_t list_length = batch->offsets[i + 1] - start;
      if (list_length && (!has_nulls || batch->notNull[i])) {
        RETURN_NOT_OK(struct_builder->AppendValues(list_length, nullptr));
        RETURN_NOT_OK(AppendBatch(keytype, keys, start, list_length,
                                  struct_builder->field_builder(0)));
        RETURN_NOT_OK(AppendBatch(valtype, vals, start, list_length,
                                  struct_builder->field_builder(1)));
      }
    }
    return Status::OK();
  }

  template <class builder_type, class batch_type, class elem_type>
  Status AppendNumericBatch(liborc::ColumnVectorBatch* cbatch, int64_t offset,
                            int64_t length, ArrayBuilder* abuilder) {
    auto builder = checked_cast<builder_type*>(abuilder);
    auto batch = checked_cast<batch_type*>(cbatch);

    if (length == 0) {
      return Status::OK();
    }
    const uint8_t* valid_bytes = nullptr;
    if (batch->hasNulls) {
      valid_bytes = reinterpret_cast<const uint8_t*>(batch->notNull.data()) + offset;
    }
    const elem_type* source = batch->data.data() + offset;
    RETURN_NOT_OK(builder->AppendValues(source, length, valid_bytes));
    return Status::OK();
  }

  template <class builder_type, class target_type, class batch_type, class source_type>
  Status AppendNumericBatchCast(liborc::ColumnVectorBatch* cbatch, int64_t offset,
                                int64_t length, ArrayBuilder* abuilder) {
    auto builder = checked_cast<builder_type*>(abuilder);
    auto batch = checked_cast<batch_type*>(cbatch);

    if (length == 0) {
      return Status::OK();
    }

    const uint8_t* valid_bytes = nullptr;
    if (batch->hasNulls) {
      valid_bytes = reinterpret_cast<const uint8_t*>(batch->notNull.data()) + offset;
    }
    const source_type* source = batch->data.data() + offset;
    auto cast_iter = internal::MakeLazyRange(
        [&source](int64_t index) { return static_cast<target_type>(source[index]); },
        length);

    RETURN_NOT_OK(builder->AppendValues(cast_iter.begin(), cast_iter.end(), valid_bytes));

    return Status::OK();
  }

  Status AppendBoolBatch(liborc::ColumnVectorBatch* cbatch, int64_t offset,
                         int64_t length, ArrayBuilder* abuilder) {
    auto builder = checked_cast<BooleanBuilder*>(abuilder);
    auto batch = checked_cast<liborc::LongVectorBatch*>(cbatch);

    if (length == 0) {
      return Status::OK();
    }

    const uint8_t* valid_bytes = nullptr;
    if (batch->hasNulls) {
      valid_bytes = reinterpret_cast<const uint8_t*>(batch->notNull.data()) + offset;
    }
    const int64_t* source = batch->data.data() + offset;

    auto cast_iter = internal::MakeLazyRange(
        [&source](int64_t index) { return static_cast<bool>(source[index]); }, length);

    RETURN_NOT_OK(builder->AppendValues(cast_iter.begin(), cast_iter.end(), valid_bytes));

    return Status::OK();
  }

  Status AppendTimestampBatch(liborc::ColumnVectorBatch* cbatch, int64_t offset,
                              int64_t length, ArrayBuilder* abuilder) {
    auto builder = checked_cast<TimestampBuilder*>(abuilder);
    auto batch = checked_cast<liborc::TimestampVectorBatch*>(cbatch);

    if (length == 0) {
      return Status::OK();
    }

    const uint8_t* valid_bytes = nullptr;
    if (batch->hasNulls) {
      valid_bytes = reinterpret_cast<const uint8_t*>(batch->notNull.data()) + offset;
    }

    const int64_t* seconds = batch->data.data() + offset;
    const int64_t* nanos = batch->nanoseconds.data() + offset;

    auto transform_timestamp = [seconds, nanos](int64_t index) {
      return seconds[index] * kOneSecondNanos + nanos[index];
    };

    auto transform_range = internal::MakeLazyRange(transform_timestamp, length);

    RETURN_NOT_OK(builder->AppendValues(transform_range.begin(), transform_range.end(),
                                        valid_bytes));
    return Status::OK();
  }

  template <class builder_type>
  Status AppendBinaryBatch(liborc::ColumnVectorBatch* cbatch, int64_t offset,
                           int64_t length, ArrayBuilder* abuilder) {
    auto builder = checked_cast<builder_type*>(abuilder);
    auto batch = checked_cast<liborc::StringVectorBatch*>(cbatch);

    const bool has_nulls = batch->hasNulls;
    for (int64_t i = offset; i < length + offset; i++) {
      if (!has_nulls || batch->notNull[i]) {
        RETURN_NOT_OK(
            builder->Append(batch->data[i], static_cast<int32_t>(batch->length[i])));
      } else {
        RETURN_NOT_OK(builder->AppendNull());
      }
    }
    return Status::OK();
  }

  Status AppendFixedBinaryBatch(liborc::ColumnVectorBatch* cbatch, int64_t offset,
                                int64_t length, ArrayBuilder* abuilder) {
    auto builder = checked_cast<FixedSizeBinaryBuilder*>(abuilder);
    auto batch = checked_cast<liborc::StringVectorBatch*>(cbatch);

    const bool has_nulls = batch->hasNulls;
    for (int64_t i = offset; i < length + offset; i++) {
      if (!has_nulls || batch->notNull[i]) {
        RETURN_NOT_OK(builder->Append(batch->data[i]));
      } else {
        RETURN_NOT_OK(builder->AppendNull());
      }
    }
    return Status::OK();
  }

  Status AppendDecimalBatch(const liborc::Type* type, liborc::ColumnVectorBatch* cbatch,
                            int64_t offset, int64_t length, ArrayBuilder* abuilder) {
    auto builder = checked_cast<Decimal128Builder*>(abuilder);

    const bool has_nulls = cbatch->hasNulls;
    if (type->getPrecision() == 0 || type->getPrecision() > 18) {
      auto batch = checked_cast<liborc::Decimal128VectorBatch*>(cbatch);
      for (int64_t i = offset; i < length + offset; i++) {
        if (!has_nulls || batch->notNull[i]) {
          RETURN_NOT_OK(builder->Append(
              Decimal128(batch->values[i].getHighBits(), batch->values[i].getLowBits())));
        } else {
          RETURN_NOT_OK(builder->AppendNull());
        }
      }
    } else {
      auto batch = checked_cast<liborc::Decimal64VectorBatch*>(cbatch);
      for (int64_t i = offset; i < length + offset; i++) {
        if (!has_nulls || batch->notNull[i]) {
          RETURN_NOT_OK(builder->Append(Decimal128(batch->values[i])));
        } else {
          RETURN_NOT_OK(builder->AppendNull());
        }
      }
    }
    return Status::OK();
  }

 private:
  MemoryPool* pool_;
  std::unique_ptr<liborc::Reader> reader_;
  std::vector<StripeInformation> stripes_;
};

ORCFileReader::ORCFileReader() { impl_.reset(new ORCFileReader::Impl()); }

ORCFileReader::~ORCFileReader() {}

Status ORCFileReader::Open(const std::shared_ptr<io::ReadableFileInterface>& file,
                           MemoryPool* pool, std::unique_ptr<ORCFileReader>* reader) {
  auto result = std::unique_ptr<ORCFileReader>(new ORCFileReader());
  RETURN_NOT_OK(result->impl_->Open(file, pool));
  *reader = std::move(result);
  return Status::OK();
}

Status ORCFileReader::ReadSchema(std::shared_ptr<Schema>* out) {
  return impl_->ReadSchema(out);
}

Status ORCFileReader::Read(std::shared_ptr<Table>* out) { return impl_->Read(out); }

Status ORCFileReader::Read(const std::shared_ptr<Schema>& schema,
                           std::shared_ptr<Table>* out) {
  return impl_->Read(schema, out);
}

Status ORCFileReader::Read(const std::vector<int>& include_indices,
                           std::shared_ptr<Table>* out) {
  return impl_->Read(include_indices, out);
}

Status ORCFileReader::Read(const std::shared_ptr<Schema>& schema,
                           const std::vector<int>& include_indices,
                           std::shared_ptr<Table>* out) {
  return impl_->Read(schema, include_indices, out);
}

Status ORCFileReader::ReadStripe(int64_t stripe, std::shared_ptr<RecordBatch>* out) {
  return impl_->ReadStripe(stripe, out);
}

Status ORCFileReader::ReadStripe(int64_t stripe, const std::vector<int>& include_indices,
                                 std::shared_ptr<RecordBatch>* out) {
  return impl_->ReadStripe(stripe, include_indices, out);
}

int64_t ORCFileReader::NumberOfStripes() { return impl_->NumberOfStripes(); }

int64_t ORCFileReader::NumberOfRows() { return impl_->NumberOfRows(); }

}  // namespace orc
}  // namespace adapters
}  // namespace arrow
