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

// Object model for scalar (non-Array) values. Not intended for use with large
// amounts of data
//
// NOTE: This API is experimental as of the 0.13 version and subject to change
// without deprecation warnings

#pragma once

#include <memory>
#include <vector>

#include "arrow/type.h"
#include "arrow/type_fwd.h"
#include "arrow/type_traits.h"
#include "arrow/util/decimal.h"
#include "arrow/util/variant.h"
#include "arrow/util/visibility.h"

namespace arrow {

class Array;

/// \brief Base class for scalar values, representing a single value occupying
/// an array "slot"
struct ARROW_EXPORT Scalar {
  virtual ~Scalar() = default;

  /// \brief The type of the scalar value
  std::shared_ptr<DataType> type;

  /// \brief Whether the value is valid (not null) or not
  bool is_valid;

 protected:
  Scalar(const std::shared_ptr<DataType>& type, bool is_valid)
      : type(type), is_valid(is_valid) {}
};

/// \brief A scalar value for NullType. Never valid
struct ARROW_EXPORT NullScalar : public Scalar {
 public:
  NullScalar() : Scalar{null(), false} {}
};

struct ARROW_EXPORT BooleanScalar : public Scalar {
  bool value;
  explicit BooleanScalar(bool value, bool is_valid = true)
      : Scalar{boolean(), is_valid}, value(value) {}
};

template <typename Type>
struct NumericScalar : public Scalar {
  using T = typename Type::c_type;
  T value;

  explicit NumericScalar(T value, bool is_valid = true)
      : NumericScalar(value, TypeTraits<Type>::type_singleton(), is_valid) {}

 protected:
  explicit NumericScalar(T value, const std::shared_ptr<DataType>& type, bool is_valid)
      : Scalar{type, is_valid}, value(value) {}
};

struct ARROW_EXPORT BinaryScalar : public Scalar {
  std::shared_ptr<Buffer> value;
  explicit BinaryScalar(const std::shared_ptr<Buffer>& value, bool is_valid = true)
      : BinaryScalar(value, binary(), is_valid) {}

 protected:
  BinaryScalar(const std::shared_ptr<Buffer>& value,
               const std::shared_ptr<DataType>& type, bool is_valid = true)
      : Scalar{type, is_valid}, value(value) {}
};

struct ARROW_EXPORT FixedSizeBinaryScalar : public BinaryScalar {
  FixedSizeBinaryScalar(const std::shared_ptr<Buffer>& value,
                        const std::shared_ptr<DataType>& type, bool is_valid = true);
};

struct ARROW_EXPORT StringScalar : public BinaryScalar {
  explicit StringScalar(const std::shared_ptr<Buffer>& value, bool is_valid = true)
      : BinaryScalar(value, utf8(), is_valid) {}
};

class ARROW_EXPORT Date32Scalar : public NumericScalar<Date32Type> {
 public:
  using NumericScalar<Date32Type>::NumericScalar;
};

class ARROW_EXPORT Date64Scalar : public NumericScalar<Date64Type> {
 public:
  using NumericScalar<Date64Type>::NumericScalar;
};

class ARROW_EXPORT Time32Scalar : public Scalar {
 public:
  int32_t value;
  Time32Scalar(int32_t value, const std::shared_ptr<DataType>& type,
               bool is_valid = true);
};

class ARROW_EXPORT Time64Scalar : public Scalar {
 public:
  int64_t value;
  Time64Scalar(int64_t value, const std::shared_ptr<DataType>& type,
               bool is_valid = true);
};

class ARROW_EXPORT TimestampScalar : public Scalar {
 public:
  int64_t value;
  TimestampScalar(int64_t value, const std::shared_ptr<DataType>& type,
                  bool is_valid = true);
};

struct ARROW_EXPORT Decimal128Scalar : public Scalar {
  Decimal128 value;
  Decimal128Scalar(const Decimal128& value, const std::shared_ptr<DataType>& type,
                   bool is_valid = true);
};

struct ARROW_EXPORT ListScalar : public Scalar {
  std::shared_ptr<Array> value;

  ListScalar(const std::shared_ptr<Array>& value, const std::shared_ptr<DataType>& type,
             bool is_valid = true);

  explicit ListScalar(const std::shared_ptr<Array>& value, bool is_valid = true);
};

struct ARROW_EXPORT StructScalar : public Scalar {
  std::vector<std::shared_ptr<Scalar>> value;
};

}  // namespace arrow
