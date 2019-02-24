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

#include <cstdint>
#include <cstdio>
#include <functional>
#include <locale>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "arrow/array.h"
#include "arrow/buffer.h"
#include "arrow/memory_pool.h"
#include "arrow/status.h"
#include "arrow/table.h"
#include "arrow/testing/gtest_common.h"
#include "arrow/testing/gtest_util.h"
#include "arrow/type.h"
#include "arrow/type_fwd.h"
#include "arrow/type_traits.h"
#include "arrow/util/decimal.h"

#include "arrow/compute/context.h"
#include "arrow/compute/kernel.h"
#include "arrow/compute/kernels/cast.h"
#include "arrow/compute/kernels/hash.h"
#include "arrow/compute/kernels/util-internal.h"
#include "arrow/compute/test-util.h"

using std::shared_ptr;
using std::vector;

namespace arrow {
namespace compute {

static std::vector<std::shared_ptr<DataType>> kNumericTypes = {
    uint8(), int8(),   uint16(), int16(),   uint32(),
    int32(), uint64(), int64(),  float32(), float64()};

static void AssertBufferSame(const Array& left, const Array& right, int buffer_index) {
  ASSERT_EQ(left.data()->buffers[buffer_index].get(),
            right.data()->buffers[buffer_index].get());
}

class TestCast : public ComputeFixture, public TestBase {
 public:
  void CheckPass(const Array& input, const Array& expected,
                 const shared_ptr<DataType>& out_type, const CastOptions& options) {
    shared_ptr<Array> result;
    ASSERT_OK(Cast(&ctx_, input, out_type, options, &result));
    ASSERT_ARRAYS_EQUAL(expected, *result);
  }

  template <typename InType, typename I_TYPE>
  void CheckFails(const shared_ptr<DataType>& in_type, const vector<I_TYPE>& in_values,
                  const vector<bool>& is_valid, const shared_ptr<DataType>& out_type,
                  const CastOptions& options) {
    shared_ptr<Array> input, result;
    if (is_valid.size() > 0) {
      ArrayFromVector<InType, I_TYPE>(in_type, is_valid, in_values, &input);
    } else {
      ArrayFromVector<InType, I_TYPE>(in_type, in_values, &input);
    }
    ASSERT_RAISES(Invalid, Cast(&ctx_, *input, out_type, options, &result));
  }

  void CheckZeroCopy(const Array& input, const shared_ptr<DataType>& out_type) {
    shared_ptr<Array> result;
    ASSERT_OK(Cast(&ctx_, input, out_type, {}, &result));
    ASSERT_EQ(input.data()->buffers.size(), result->data()->buffers.size());
    for (size_t i = 0; i < input.data()->buffers.size(); ++i) {
      AssertBufferSame(input, *result, static_cast<int>(i));
    }
  }

  template <typename InType, typename I_TYPE, typename OutType, typename O_TYPE>
  void CheckCase(const shared_ptr<DataType>& in_type, const vector<I_TYPE>& in_values,
                 const vector<bool>& is_valid, const shared_ptr<DataType>& out_type,
                 const vector<O_TYPE>& out_values, const CastOptions& options) {
    DCHECK_EQ(in_values.size(), out_values.size());
    shared_ptr<Array> input, expected;
    if (is_valid.size() > 0) {
      DCHECK_EQ(is_valid.size(), out_values.size());
      ArrayFromVector<InType, I_TYPE>(in_type, is_valid, in_values, &input);
      ArrayFromVector<OutType, O_TYPE>(out_type, is_valid, out_values, &expected);
    } else {
      ArrayFromVector<InType, I_TYPE>(in_type, in_values, &input);
      ArrayFromVector<OutType, O_TYPE>(out_type, out_values, &expected);
    }
    CheckPass(*input, *expected, out_type, options);

    // Check a sliced variant
    if (input->length() > 1) {
      CheckPass(*input->Slice(1), *expected->Slice(1), out_type, options);
    }
  }

  void CheckCaseJSON(const shared_ptr<DataType>& in_type,
                     const shared_ptr<DataType>& out_type, const std::string& in_json,
                     const std::string& expected_json,
                     const CastOptions& options = CastOptions()) {
    shared_ptr<Array> input = ArrayFromJSON(in_type, in_json);
    shared_ptr<Array> expected = ArrayFromJSON(out_type, expected_json);
    DCHECK_EQ(input->length(), expected->length());
    CheckPass(*input, *expected, out_type, options);

    // Check a sliced variant
    if (input->length() > 1) {
      CheckPass(*input->Slice(1), *expected->Slice(1), out_type, options);
    }
  }
};

TEST_F(TestCast, SameTypeZeroCopy) {
  shared_ptr<Array> arr = ArrayFromJSON(int32(), "[0, null, 2, 3, 4]");
  shared_ptr<Array> result;
  ASSERT_OK(Cast(&this->ctx_, *arr, int32(), {}, &result));

  AssertBufferSame(*arr, *result, 0);
  AssertBufferSame(*arr, *result, 1);
}

TEST_F(TestCast, FromBoolean) {
  CastOptions options;

  vector<bool> is_valid(20, true);
  is_valid[3] = false;

  vector<bool> v1(is_valid.size(), true);
  vector<int32_t> e1(is_valid.size(), 1);
  for (size_t i = 0; i < v1.size(); ++i) {
    if (i % 3 == 1) {
      v1[i] = false;
      e1[i] = 0;
    }
  }

  CheckCase<BooleanType, bool, Int32Type, int32_t>(boolean(), v1, is_valid, int32(), e1,
                                                   options);
}

TEST_F(TestCast, ToBoolean) {
  CastOptions options;
  for (auto type : kNumericTypes) {
    CheckCaseJSON(type, boolean(), "[0, null, 127, 1, 0]",
                  "[false, null, true, true, false]");
  }

  // Check negative numbers
  CheckCaseJSON(int8(), boolean(), "[0, null, 127, -1, 0]",
                "[false, null, true, true, false]");
  CheckCaseJSON(float64(), boolean(), "[0, null, 127, -1, 0]",
                "[false, null, true, true, false]");
}

TEST_F(TestCast, ToIntUpcast) {
  CastOptions options;
  options.allow_int_overflow = false;

  vector<bool> is_valid = {true, false, true, true, true};

  // int8 to int32
  vector<int8_t> v1 = {0, 1, 127, -1, 0};
  vector<int32_t> e1 = {0, 1, 127, -1, 0};
  CheckCase<Int8Type, int8_t, Int32Type, int32_t>(int8(), v1, is_valid, int32(), e1,
                                                  options);

  // bool to int8
  vector<bool> v2 = {false, true, false, true, true};
  vector<int8_t> e2 = {0, 1, 0, 1, 1};
  CheckCase<BooleanType, bool, Int8Type, int8_t>(boolean(), v2, is_valid, int8(), e2,
                                                 options);

  // uint8 to int16, no overflow/underrun
  vector<uint8_t> v3 = {0, 100, 200, 255, 0};
  vector<int16_t> e3 = {0, 100, 200, 255, 0};
  CheckCase<UInt8Type, uint8_t, Int16Type, int16_t>(uint8(), v3, is_valid, int16(), e3,
                                                    options);
}

TEST_F(TestCast, OverflowInNullSlot) {
  CastOptions options;
  options.allow_int_overflow = false;

  vector<bool> is_valid = {true, false, true, true, true};

  vector<int32_t> v11 = {0, 70000, 2000, 1000, 0};
  vector<int16_t> e11 = {0, 0, 2000, 1000, 0};

  shared_ptr<Array> expected;
  ArrayFromVector<Int16Type, int16_t>(int16(), is_valid, e11, &expected);

  auto buf = Buffer::Wrap(v11.data(), v11.size());
  Int32Array tmp11(5, buf, expected->null_bitmap(), -1);

  CheckPass(tmp11, *expected, int16(), options);
}

TEST_F(TestCast, ToIntDowncastSafe) {
  CastOptions options;
  options.allow_int_overflow = false;

  vector<bool> is_valid = {true, false, true, true, true};

  // int16 to uint8, no overflow/underrun
  vector<int16_t> v1 = {0, 100, 200, 1, 2};
  vector<uint8_t> e1 = {0, 100, 200, 1, 2};
  CheckCase<Int16Type, int16_t, UInt8Type, uint8_t>(int16(), v1, is_valid, uint8(), e1,
                                                    options);

  // int16 to uint8, with overflow
  vector<int16_t> v2 = {0, 100, 256, 0, 0};
  CheckFails<Int16Type>(int16(), v2, is_valid, uint8(), options);

  // underflow
  vector<int16_t> v3 = {0, 100, -1, 0, 0};
  CheckFails<Int16Type>(int16(), v3, is_valid, uint8(), options);

  // int32 to int16, no overflow
  vector<int32_t> v4 = {0, 1000, 2000, 1, 2};
  vector<int16_t> e4 = {0, 1000, 2000, 1, 2};
  CheckCase<Int32Type, int32_t, Int16Type, int16_t>(int32(), v4, is_valid, int16(), e4,
                                                    options);

  // int32 to int16, overflow
  vector<int32_t> v5 = {0, 1000, 2000, 70000, 0};
  CheckFails<Int32Type>(int32(), v5, is_valid, int16(), options);

  // underflow
  vector<int32_t> v6 = {0, 1000, 2000, -70000, 0};
  CheckFails<Int32Type>(int32(), v6, is_valid, int16(), options);

  vector<int32_t> v7 = {0, 1000, 2000, -70000, 0};
  CheckFails<Int32Type>(int32(), v7, is_valid, uint8(), options);
}

template <typename O, typename I>
std::vector<O> UnsafeVectorCast(const std::vector<I>& v) {
  size_t n_elems = v.size();
  std::vector<O> result(n_elems);

  for (size_t i = 0; i < v.size(); i++) result[i] = static_cast<O>(v[i]);

  return std::move(result);
}

TEST_F(TestCast, IntegerSignedToUnsigned) {
  CastOptions options;
  options.allow_int_overflow = false;

  vector<bool> is_valid = {true, false, true, true, true};

  vector<int32_t> v1 = {INT32_MIN, 100, -1, UINT16_MAX, INT32_MAX};

  // Same width
  CheckFails<Int32Type>(int32(), v1, is_valid, uint32(), options);
  // Wider
  CheckFails<Int32Type>(int32(), v1, is_valid, uint64(), options);
  // Narrower
  CheckFails<Int32Type>(int32(), v1, is_valid, uint16(), options);
  // Fail because of overflow (instead of underflow).
  vector<int32_t> over = {0, -11, 0, UINT16_MAX + 1, INT32_MAX};
  CheckFails<Int32Type>(int32(), over, is_valid, uint16(), options);

  options.allow_int_overflow = true;

  CheckCase<Int32Type, int32_t, UInt32Type, uint32_t>(
      int32(), v1, is_valid, uint32(), UnsafeVectorCast<uint32_t, int32_t>(v1), options);
  CheckCase<Int32Type, int32_t, UInt64Type, uint64_t>(
      int32(), v1, is_valid, uint64(), UnsafeVectorCast<uint64_t, int32_t>(v1), options);
  CheckCase<Int32Type, int32_t, UInt16Type, uint16_t>(
      int32(), v1, is_valid, uint16(), UnsafeVectorCast<uint16_t, int32_t>(v1), options);
  CheckCase<Int32Type, int32_t, UInt16Type, uint16_t>(
      int32(), over, is_valid, uint16(), UnsafeVectorCast<uint16_t, int32_t>(over),
      options);
}

TEST_F(TestCast, IntegerUnsignedToSigned) {
  CastOptions options;
  options.allow_int_overflow = false;

  vector<bool> is_valid = {true, true, true};

  vector<uint32_t> v1 = {0, INT16_MAX + 1, UINT32_MAX};
  vector<uint32_t> v2 = {0, INT16_MAX + 1, 2};
  // Same width
  CheckFails<UInt32Type>(uint32(), v1, is_valid, int32(), options);
  // Narrower
  CheckFails<UInt32Type>(uint32(), v1, is_valid, int16(), options);
  CheckFails<UInt32Type>(uint32(), v2, is_valid, int16(), options);

  options.allow_int_overflow = true;

  CheckCase<UInt32Type, uint32_t, Int32Type, int32_t>(
      uint32(), v1, is_valid, int32(), UnsafeVectorCast<int32_t, uint32_t>(v1), options);
  CheckCase<UInt32Type, uint32_t, Int64Type, int64_t>(
      uint32(), v1, is_valid, int64(), UnsafeVectorCast<int64_t, uint32_t>(v1), options);
  CheckCase<UInt32Type, uint32_t, Int16Type, int16_t>(
      uint32(), v1, is_valid, int16(), UnsafeVectorCast<int16_t, uint32_t>(v1), options);
  CheckCase<UInt32Type, uint32_t, Int16Type, int16_t>(
      uint32(), v2, is_valid, int16(), UnsafeVectorCast<int16_t, uint32_t>(v2), options);
}

TEST_F(TestCast, ToIntDowncastUnsafe) {
  CastOptions options;
  options.allow_int_overflow = true;

  vector<bool> is_valid = {true, false, true, true, true};

  // int16 to uint8, no overflow/underrun
  vector<int16_t> v1 = {0, 100, 200, 1, 2};
  vector<uint8_t> e1 = {0, 100, 200, 1, 2};
  CheckCase<Int16Type, int16_t, UInt8Type, uint8_t>(int16(), v1, is_valid, uint8(), e1,
                                                    options);

  // int16 to uint8, with overflow
  vector<int16_t> v2 = {0, 100, 256, 0, 0};
  vector<uint8_t> e2 = {0, 100, 0, 0, 0};
  CheckCase<Int16Type, int16_t, UInt8Type, uint8_t>(int16(), v2, is_valid, uint8(), e2,
                                                    options);

  // underflow
  vector<int16_t> v3 = {0, 100, -1, 0, 0};
  vector<uint8_t> e3 = {0, 100, 255, 0, 0};
  CheckCase<Int16Type, int16_t, UInt8Type, uint8_t>(int16(), v3, is_valid, uint8(), e3,
                                                    options);

  // int32 to int16, no overflow
  vector<int32_t> v4 = {0, 1000, 2000, 1, 2};
  vector<int16_t> e4 = {0, 1000, 2000, 1, 2};
  CheckCase<Int32Type, int32_t, Int16Type, int16_t>(int32(), v4, is_valid, int16(), e4,
                                                    options);

  // int32 to int16, overflow
  // TODO(wesm): do we want to allow this? we could set to null
  vector<int32_t> v5 = {0, 1000, 2000, 70000, 0};
  vector<int16_t> e5 = {0, 1000, 2000, 4464, 0};
  CheckCase<Int32Type, int32_t, Int16Type, int16_t>(int32(), v5, is_valid, int16(), e5,
                                                    options);

  // underflow
  // TODO(wesm): do we want to allow this? we could set overflow to null
  vector<int32_t> v6 = {0, 1000, 2000, -70000, 0};
  vector<int16_t> e6 = {0, 1000, 2000, -4464, 0};
  CheckCase<Int32Type, int32_t, Int16Type, int16_t>(int32(), v6, is_valid, int16(), e6,
                                                    options);
}

TEST_F(TestCast, FloatingPointToInt) {
  // which means allow_float_truncate == false
  auto options = CastOptions::Safe();

  vector<bool> is_valid = {true, false, true, true, true};
  vector<bool> all_valid = {true, true, true, true, true};

  // float32 to int32 no truncation
  vector<float> v1 = {1.0, 0, 0.0, -1.0, 5.0};
  vector<int32_t> e1 = {1, 0, 0, -1, 5};
  CheckCase<FloatType, float, Int32Type, int32_t>(float32(), v1, is_valid, int32(), e1,
                                                  options);
  CheckCase<FloatType, float, Int32Type, int32_t>(float32(), v1, all_valid, int32(), e1,
                                                  options);

  // float64 to int32 no truncation
  vector<double> v2 = {1.0, 0, 0.0, -1.0, 5.0};
  vector<int32_t> e2 = {1, 0, 0, -1, 5};
  CheckCase<DoubleType, double, Int32Type, int32_t>(float64(), v2, is_valid, int32(), e2,
                                                    options);
  CheckCase<DoubleType, double, Int32Type, int32_t>(float64(), v2, all_valid, int32(), e2,
                                                    options);

  // float64 to int64 no truncation
  vector<double> v3 = {1.0, 0, 0.0, -1.0, 5.0};
  vector<int64_t> e3 = {1, 0, 0, -1, 5};
  CheckCase<DoubleType, double, Int64Type, int64_t>(float64(), v3, is_valid, int64(), e3,
                                                    options);
  CheckCase<DoubleType, double, Int64Type, int64_t>(float64(), v3, all_valid, int64(), e3,
                                                    options);

  // float64 to int32 truncate
  vector<double> v4 = {1.5, 0, 0.5, -1.5, 5.5};
  vector<int32_t> e4 = {1, 0, 0, -1, 5};

  options.allow_float_truncate = false;
  CheckFails<DoubleType>(float64(), v4, is_valid, int32(), options);
  CheckFails<DoubleType>(float64(), v4, all_valid, int32(), options);

  options.allow_float_truncate = true;
  CheckCase<DoubleType, double, Int32Type, int32_t>(float64(), v4, is_valid, int32(), e4,
                                                    options);
  CheckCase<DoubleType, double, Int32Type, int32_t>(float64(), v4, all_valid, int32(), e4,
                                                    options);

  // float64 to int64 truncate
  vector<double> v5 = {1.5, 0, 0.5, -1.5, 5.5};
  vector<int64_t> e5 = {1, 0, 0, -1, 5};

  options.allow_float_truncate = false;
  CheckFails<DoubleType>(float64(), v5, is_valid, int64(), options);
  CheckFails<DoubleType>(float64(), v5, all_valid, int64(), options);

  options.allow_float_truncate = true;
  CheckCase<DoubleType, double, Int64Type, int64_t>(float64(), v5, is_valid, int64(), e5,
                                                    options);
  CheckCase<DoubleType, double, Int64Type, int64_t>(float64(), v5, all_valid, int64(), e5,
                                                    options);
}

TEST_F(TestCast, IntToFloatingPoint) {
  auto options = CastOptions::Safe();

  vector<bool> all_valid = {true, true, true, true, true};
  vector<bool> all_invalid = {false, false, false, false, false};

  vector<int64_t> v1 = {INT64_MIN, INT64_MIN + 1, 0, INT64_MAX - 1, INT64_MAX};
  CheckFails<Int64Type>(int64(), v1, all_valid, float32(), options);

  // While it's not safe to convert, all values are null.
  CheckCase<Int64Type, int64_t, DoubleType, double>(int64(), v1, all_invalid, float64(),
                                                    UnsafeVectorCast<double, int64_t>(v1),
                                                    options);
}

TEST_F(TestCast, TimestampToTimestamp) {
  CastOptions options;

  auto CheckTimestampCast =
      [this](const CastOptions& options, TimeUnit::type from_unit, TimeUnit::type to_unit,
             const vector<int64_t>& from_values, const vector<int64_t>& to_values,
             const vector<bool>& is_valid) {
        CheckCase<TimestampType, int64_t, TimestampType, int64_t>(
            timestamp(from_unit), from_values, is_valid, timestamp(to_unit), to_values,
            options);
      };

  vector<bool> is_valid = {true, false, true, true, true};

  // Multiply promotions
  vector<int64_t> v1 = {0, 100, 200, 1, 2};
  vector<int64_t> e1 = {0, 100000, 200000, 1000, 2000};
  CheckTimestampCast(options, TimeUnit::SECOND, TimeUnit::MILLI, v1, e1, is_valid);

  vector<int64_t> v2 = {0, 100, 200, 1, 2};
  vector<int64_t> e2 = {0, 100000000L, 200000000L, 1000000, 2000000};
  CheckTimestampCast(options, TimeUnit::SECOND, TimeUnit::MICRO, v2, e2, is_valid);

  vector<int64_t> v3 = {0, 100, 200, 1, 2};
  vector<int64_t> e3 = {0, 100000000000L, 200000000000L, 1000000000L, 2000000000L};
  CheckTimestampCast(options, TimeUnit::SECOND, TimeUnit::NANO, v3, e3, is_valid);

  vector<int64_t> v4 = {0, 100, 200, 1, 2};
  vector<int64_t> e4 = {0, 100000, 200000, 1000, 2000};
  CheckTimestampCast(options, TimeUnit::MILLI, TimeUnit::MICRO, v4, e4, is_valid);

  vector<int64_t> v5 = {0, 100, 200, 1, 2};
  vector<int64_t> e5 = {0, 100000000L, 200000000L, 1000000, 2000000};
  CheckTimestampCast(options, TimeUnit::MILLI, TimeUnit::NANO, v5, e5, is_valid);

  vector<int64_t> v6 = {0, 100, 200, 1, 2};
  vector<int64_t> e6 = {0, 100000, 200000, 1000, 2000};
  CheckTimestampCast(options, TimeUnit::MICRO, TimeUnit::NANO, v6, e6, is_valid);

  // Zero copy
  vector<int64_t> v7 = {0, 70000, 2000, 1000, 0};
  shared_ptr<Array> arr;
  ArrayFromVector<TimestampType, int64_t>(timestamp(TimeUnit::SECOND), is_valid, v7,
                                          &arr);
  CheckZeroCopy(*arr, timestamp(TimeUnit::SECOND));

  // ARROW-1773, cast to integer
  CheckZeroCopy(*arr, int64());

  // Divide, truncate
  vector<int64_t> v8 = {0, 100123, 200456, 1123, 2456};
  vector<int64_t> e8 = {0, 100, 200, 1, 2};

  options.allow_time_truncate = true;
  CheckTimestampCast(options, TimeUnit::MILLI, TimeUnit::SECOND, v8, e8, is_valid);
  CheckTimestampCast(options, TimeUnit::MICRO, TimeUnit::MILLI, v8, e8, is_valid);
  CheckTimestampCast(options, TimeUnit::NANO, TimeUnit::MICRO, v8, e8, is_valid);

  vector<int64_t> v9 = {0, 100123000, 200456000, 1123000, 2456000};
  vector<int64_t> e9 = {0, 100, 200, 1, 2};
  CheckTimestampCast(options, TimeUnit::MICRO, TimeUnit::SECOND, v9, e9, is_valid);
  CheckTimestampCast(options, TimeUnit::NANO, TimeUnit::MILLI, v9, e9, is_valid);

  vector<int64_t> v10 = {0, 100123000000L, 200456000000L, 1123000000L, 2456000000};
  vector<int64_t> e10 = {0, 100, 200, 1, 2};
  CheckTimestampCast(options, TimeUnit::NANO, TimeUnit::SECOND, v10, e10, is_valid);

  // Disallow truncate, failures
  options.allow_time_truncate = false;
  CheckFails<TimestampType>(timestamp(TimeUnit::MILLI), v8, is_valid,
                            timestamp(TimeUnit::SECOND), options);
  CheckFails<TimestampType>(timestamp(TimeUnit::MICRO), v8, is_valid,
                            timestamp(TimeUnit::MILLI), options);
  CheckFails<TimestampType>(timestamp(TimeUnit::NANO), v8, is_valid,
                            timestamp(TimeUnit::MICRO), options);
  CheckFails<TimestampType>(timestamp(TimeUnit::MICRO), v9, is_valid,
                            timestamp(TimeUnit::SECOND), options);
  CheckFails<TimestampType>(timestamp(TimeUnit::NANO), v9, is_valid,
                            timestamp(TimeUnit::MILLI), options);
  CheckFails<TimestampType>(timestamp(TimeUnit::NANO), v10, is_valid,
                            timestamp(TimeUnit::SECOND), options);
}

TEST_F(TestCast, TimestampToDate32_Date64) {
  CastOptions options;

  vector<bool> is_valid = {true, true, false};

  // 2000-01-01, 2000-01-02, null
  vector<int64_t> v_nano = {946684800000000000, 946771200000000000, 0};
  vector<int64_t> v_micro = {946684800000000, 946771200000000, 0};
  vector<int64_t> v_milli = {946684800000, 946771200000, 0};
  vector<int64_t> v_second = {946684800, 946771200, 0};
  vector<int32_t> v_day = {10957, 10958, 0};

  // Simple conversions
  CheckCase<TimestampType, int64_t, Date64Type, int64_t>(
      timestamp(TimeUnit::NANO), v_nano, is_valid, date64(), v_milli, options);
  CheckCase<TimestampType, int64_t, Date64Type, int64_t>(
      timestamp(TimeUnit::MICRO), v_micro, is_valid, date64(), v_milli, options);
  CheckCase<TimestampType, int64_t, Date64Type, int64_t>(
      timestamp(TimeUnit::MILLI), v_milli, is_valid, date64(), v_milli, options);
  CheckCase<TimestampType, int64_t, Date64Type, int64_t>(
      timestamp(TimeUnit::SECOND), v_second, is_valid, date64(), v_milli, options);

  CheckCase<TimestampType, int64_t, Date32Type, int32_t>(
      timestamp(TimeUnit::NANO), v_nano, is_valid, date32(), v_day, options);
  CheckCase<TimestampType, int64_t, Date32Type, int32_t>(
      timestamp(TimeUnit::MICRO), v_micro, is_valid, date32(), v_day, options);
  CheckCase<TimestampType, int64_t, Date32Type, int32_t>(
      timestamp(TimeUnit::MILLI), v_milli, is_valid, date32(), v_day, options);
  CheckCase<TimestampType, int64_t, Date32Type, int32_t>(
      timestamp(TimeUnit::SECOND), v_second, is_valid, date32(), v_day, options);

  // Disallow truncate, failures
  vector<int64_t> v_nano_fail = {946684800000000001, 946771200000000001, 0};
  vector<int64_t> v_micro_fail = {946684800000001, 946771200000001, 0};
  vector<int64_t> v_milli_fail = {946684800001, 946771200001, 0};
  vector<int64_t> v_second_fail = {946684801, 946771201, 0};

  options.allow_time_truncate = false;
  CheckFails<TimestampType>(timestamp(TimeUnit::NANO), v_nano_fail, is_valid, date64(),
                            options);
  CheckFails<TimestampType>(timestamp(TimeUnit::MICRO), v_micro_fail, is_valid, date64(),
                            options);
  CheckFails<TimestampType>(timestamp(TimeUnit::MILLI), v_milli_fail, is_valid, date64(),
                            options);
  CheckFails<TimestampType>(timestamp(TimeUnit::SECOND), v_second_fail, is_valid,
                            date64(), options);

  CheckFails<TimestampType>(timestamp(TimeUnit::NANO), v_nano_fail, is_valid, date32(),
                            options);
  CheckFails<TimestampType>(timestamp(TimeUnit::MICRO), v_micro_fail, is_valid, date32(),
                            options);
  CheckFails<TimestampType>(timestamp(TimeUnit::MILLI), v_milli_fail, is_valid, date32(),
                            options);
  CheckFails<TimestampType>(timestamp(TimeUnit::SECOND), v_second_fail, is_valid,
                            date32(), options);

  // Make sure that nulls are excluded from the truncation checks
  vector<int64_t> v_second_nofail = {946684800, 946771200, 1};
  CheckCase<TimestampType, int64_t, Date64Type, int64_t>(
      timestamp(TimeUnit::SECOND), v_second_nofail, is_valid, date64(), v_milli, options);
  CheckCase<TimestampType, int64_t, Date32Type, int32_t>(
      timestamp(TimeUnit::SECOND), v_second_nofail, is_valid, date32(), v_day, options);
}

TEST_F(TestCast, TimeToCompatible) {
  CastOptions options;

  vector<bool> is_valid = {true, false, true, true, true};

  // Multiply promotions
  vector<int32_t> v1 = {0, 100, 200, 1, 2};
  vector<int32_t> e1 = {0, 100000, 200000, 1000, 2000};
  CheckCase<Time32Type, int32_t, Time32Type, int32_t>(
      time32(TimeUnit::SECOND), v1, is_valid, time32(TimeUnit::MILLI), e1, options);

  vector<int32_t> v2 = {0, 100, 200, 1, 2};
  vector<int64_t> e2 = {0, 100000000L, 200000000L, 1000000, 2000000};
  CheckCase<Time32Type, int32_t, Time64Type, int64_t>(
      time32(TimeUnit::SECOND), v2, is_valid, time64(TimeUnit::MICRO), e2, options);

  vector<int32_t> v3 = {0, 100, 200, 1, 2};
  vector<int64_t> e3 = {0, 100000000000L, 200000000000L, 1000000000L, 2000000000L};
  CheckCase<Time32Type, int32_t, Time64Type, int64_t>(
      time32(TimeUnit::SECOND), v3, is_valid, time64(TimeUnit::NANO), e3, options);

  vector<int32_t> v4 = {0, 100, 200, 1, 2};
  vector<int64_t> e4 = {0, 100000, 200000, 1000, 2000};
  CheckCase<Time32Type, int32_t, Time64Type, int64_t>(
      time32(TimeUnit::MILLI), v4, is_valid, time64(TimeUnit::MICRO), e4, options);

  vector<int32_t> v5 = {0, 100, 200, 1, 2};
  vector<int64_t> e5 = {0, 100000000L, 200000000L, 1000000, 2000000};
  CheckCase<Time32Type, int32_t, Time64Type, int64_t>(
      time32(TimeUnit::MILLI), v5, is_valid, time64(TimeUnit::NANO), e5, options);

  vector<int64_t> v6 = {0, 100, 200, 1, 2};
  vector<int64_t> e6 = {0, 100000, 200000, 1000, 2000};
  CheckCase<Time64Type, int64_t, Time64Type, int64_t>(
      time64(TimeUnit::MICRO), v6, is_valid, time64(TimeUnit::NANO), e6, options);

  // Zero copy
  vector<int64_t> v7 = {0, 70000, 2000, 1000, 0};
  shared_ptr<Array> arr;
  ArrayFromVector<Time64Type, int64_t>(time64(TimeUnit::MICRO), is_valid, v7, &arr);
  CheckZeroCopy(*arr, time64(TimeUnit::MICRO));

  // ARROW-1773: cast to int64
  CheckZeroCopy(*arr, int64());

  vector<int32_t> v7_2 = {0, 70000, 2000, 1000, 0};
  ArrayFromVector<Time32Type, int32_t>(time32(TimeUnit::SECOND), is_valid, v7_2, &arr);
  CheckZeroCopy(*arr, time32(TimeUnit::SECOND));

  // ARROW-1773: cast to int64
  CheckZeroCopy(*arr, int32());

  // Divide, truncate
  vector<int32_t> v8 = {0, 100123, 200456, 1123, 2456};
  vector<int32_t> e8 = {0, 100, 200, 1, 2};

  options.allow_time_truncate = true;
  CheckCase<Time32Type, int32_t, Time32Type, int32_t>(
      time32(TimeUnit::MILLI), v8, is_valid, time32(TimeUnit::SECOND), e8, options);
  CheckCase<Time64Type, int32_t, Time32Type, int32_t>(
      time64(TimeUnit::MICRO), v8, is_valid, time32(TimeUnit::MILLI), e8, options);
  CheckCase<Time64Type, int32_t, Time64Type, int32_t>(
      time64(TimeUnit::NANO), v8, is_valid, time64(TimeUnit::MICRO), e8, options);

  vector<int64_t> v9 = {0, 100123000, 200456000, 1123000, 2456000};
  vector<int32_t> e9 = {0, 100, 200, 1, 2};
  CheckCase<Time64Type, int64_t, Time32Type, int32_t>(
      time64(TimeUnit::MICRO), v9, is_valid, time32(TimeUnit::SECOND), e9, options);
  CheckCase<Time64Type, int64_t, Time32Type, int32_t>(
      time64(TimeUnit::NANO), v9, is_valid, time32(TimeUnit::MILLI), e9, options);

  vector<int64_t> v10 = {0, 100123000000L, 200456000000L, 1123000000L, 2456000000};
  vector<int32_t> e10 = {0, 100, 200, 1, 2};
  CheckCase<Time64Type, int64_t, Time32Type, int32_t>(
      time64(TimeUnit::NANO), v10, is_valid, time32(TimeUnit::SECOND), e10, options);

  // Disallow truncate, failures

  options.allow_time_truncate = false;
  CheckFails<Time32Type>(time32(TimeUnit::MILLI), v8, is_valid, time32(TimeUnit::SECOND),
                         options);
  CheckFails<Time64Type>(time64(TimeUnit::MICRO), v8, is_valid, time32(TimeUnit::MILLI),
                         options);
  CheckFails<Time64Type>(time64(TimeUnit::NANO), v8, is_valid, time64(TimeUnit::MICRO),
                         options);
  CheckFails<Time64Type>(time64(TimeUnit::MICRO), v9, is_valid, time32(TimeUnit::SECOND),
                         options);
  CheckFails<Time64Type>(time64(TimeUnit::NANO), v9, is_valid, time32(TimeUnit::MILLI),
                         options);
  CheckFails<Time64Type>(time64(TimeUnit::NANO), v10, is_valid, time32(TimeUnit::SECOND),
                         options);
}

TEST_F(TestCast, DateToCompatible) {
  CastOptions options;

  vector<bool> is_valid = {true, false, true, true, true};

  constexpr int64_t F = 86400000;

  // Multiply promotion
  vector<int32_t> v1 = {0, 100, 200, 1, 2};
  vector<int64_t> e1 = {0, 100 * F, 200 * F, F, 2 * F};
  CheckCase<Date32Type, int32_t, Date64Type, int64_t>(date32(), v1, is_valid, date64(),
                                                      e1, options);

  // Zero copy
  vector<int32_t> v2 = {0, 70000, 2000, 1000, 0};
  vector<int64_t> v3 = {0, 70000, 2000, 1000, 0};
  shared_ptr<Array> arr;
  ArrayFromVector<Date32Type, int32_t>(date32(), is_valid, v2, &arr);
  CheckZeroCopy(*arr, date32());

  // ARROW-1773: zero copy cast to integer
  CheckZeroCopy(*arr, int32());

  ArrayFromVector<Date64Type, int64_t>(date64(), is_valid, v3, &arr);
  CheckZeroCopy(*arr, date64());

  // ARROW-1773: zero copy cast to integer
  CheckZeroCopy(*arr, int64());

  // Divide, truncate
  vector<int64_t> v8 = {0, 100 * F + 123, 200 * F + 456, F + 123, 2 * F + 456};
  vector<int32_t> e8 = {0, 100, 200, 1, 2};

  options.allow_time_truncate = true;
  CheckCase<Date64Type, int64_t, Date32Type, int32_t>(date64(), v8, is_valid, date32(),
                                                      e8, options);

  // Disallow truncate, failures
  options.allow_time_truncate = false;
  CheckFails<Date64Type>(date64(), v8, is_valid, date32(), options);
}

TEST_F(TestCast, ToDouble) {
  CastOptions options;
  vector<bool> is_valid = {true, false, true, true, true};

  // int16 to double
  vector<int16_t> v1 = {0, 100, 200, 1, 2};
  vector<double> e1 = {0, 100, 200, 1, 2};
  CheckCase<Int16Type, int16_t, DoubleType, double>(int16(), v1, is_valid, float64(), e1,
                                                    options);

  // float to double
  vector<float> v2 = {0, 100, 200, 1, 2};
  vector<double> e2 = {0, 100, 200, 1, 2};
  CheckCase<FloatType, float, DoubleType, double>(float32(), v2, is_valid, float64(), e2,
                                                  options);

  // bool to double
  vector<bool> v3 = {true, true, false, false, true};
  vector<double> e3 = {1, 1, 0, 0, 1};
  CheckCase<BooleanType, bool, DoubleType, double>(boolean(), v3, is_valid, float64(), e3,
                                                   options);
}

TEST_F(TestCast, ChunkedArray) {
  vector<int16_t> values1 = {0, 1, 2};
  vector<int16_t> values2 = {3, 4, 5};

  auto type = int16();
  auto out_type = int64();

  auto a1 = _MakeArray<Int16Type, int16_t>(type, values1, {});
  auto a2 = _MakeArray<Int16Type, int16_t>(type, values2, {});

  ArrayVector arrays = {a1, a2};
  auto carr = std::make_shared<ChunkedArray>(arrays);

  CastOptions options;

  Datum out;
  ASSERT_OK(Cast(&this->ctx_, carr, out_type, options, &out));
  ASSERT_EQ(Datum::CHUNKED_ARRAY, out.kind());

  auto out_carr = out.chunked_array();

  vector<int64_t> ex_values1 = {0, 1, 2};
  vector<int64_t> ex_values2 = {3, 4, 5};
  auto a3 = _MakeArray<Int64Type, int64_t>(out_type, ex_values1, {});
  auto a4 = _MakeArray<Int64Type, int64_t>(out_type, ex_values2, {});

  ArrayVector ex_arrays = {a3, a4};
  auto ex_carr = std::make_shared<ChunkedArray>(ex_arrays);

  ASSERT_TRUE(out.chunked_array()->Equals(*ex_carr));
}

TEST_F(TestCast, UnsupportedTarget) {
  vector<bool> is_valid = {true, false, true, true, true};
  vector<int32_t> v1 = {0, 1, 2, 3, 4};

  shared_ptr<Array> arr;
  ArrayFromVector<Int32Type, int32_t>(int32(), is_valid, v1, &arr);

  shared_ptr<Array> result;
  ASSERT_RAISES(NotImplemented, Cast(&this->ctx_, *arr, utf8(), {}, &result));
}

TEST_F(TestCast, DateTimeZeroCopy) {
  vector<bool> is_valid = {true, false, true, true, true};

  vector<int32_t> v1 = {0, 70000, 2000, 1000, 0};
  shared_ptr<Array> arr;
  ArrayFromVector<Int32Type, int32_t>(int32(), is_valid, v1, &arr);

  CheckZeroCopy(*arr, time32(TimeUnit::SECOND));
  CheckZeroCopy(*arr, date32());

  vector<int64_t> v2 = {0, 70000, 2000, 1000, 0};
  ArrayFromVector<Int64Type, int64_t>(int64(), is_valid, v2, &arr);

  CheckZeroCopy(*arr, time64(TimeUnit::MICRO));
  CheckZeroCopy(*arr, date64());
  CheckZeroCopy(*arr, timestamp(TimeUnit::NANO));
}

TEST_F(TestCast, FromNull) {
  // Null casts to everything
  const int length = 10;

  NullArray arr(length);

  shared_ptr<Array> result;
  ASSERT_OK(Cast(&ctx_, arr, int32(), {}, &result));

  ASSERT_EQ(length, result->length());
  ASSERT_EQ(length, result->null_count());

  // OK to look at bitmaps
  ASSERT_ARRAYS_EQUAL(*result, *result);
}

TEST_F(TestCast, PreallocatedMemory) {
  CastOptions options;
  options.allow_int_overflow = false;

  vector<bool> is_valid = {true, false, true, true, true};

  const int64_t length = 5;

  shared_ptr<Array> arr;
  vector<int32_t> v1 = {0, 70000, 2000, 1000, 0};
  vector<int64_t> e1 = {0, 70000, 2000, 1000, 0};
  ArrayFromVector<Int32Type, int32_t>(int32(), is_valid, v1, &arr);

  auto out_type = int64();

  std::unique_ptr<UnaryKernel> kernel;
  ASSERT_OK(GetCastFunction(*int32(), out_type, options, &kernel));

  auto out_data = ArrayData::Make(out_type, length);

  shared_ptr<Buffer> out_values;
  ASSERT_OK(this->ctx_.Allocate(length * sizeof(int64_t), &out_values));

  out_data->buffers.push_back(arr->data()->buffers[0]);
  out_data->buffers.push_back(out_values);

  Datum out(out_data);
  ASSERT_OK(kernel->Call(&this->ctx_, arr, &out));

  // Buffer address unchanged
  ASSERT_EQ(out_values.get(), out_data->buffers[1].get());

  shared_ptr<Array> result = MakeArray(out_data);
  shared_ptr<Array> expected;
  ArrayFromVector<Int64Type, int64_t>(int64(), is_valid, e1, &expected);

  ASSERT_ARRAYS_EQUAL(*expected, *result);
}

template <typename InType, typename InT, typename OutType, typename OutT>
void CheckOffsetOutputCase(FunctionContext* ctx, const std::shared_ptr<DataType>& in_type,
                           const vector<InT>& in_values,
                           const std::shared_ptr<DataType>& out_type,
                           const vector<OutT>& out_values) {
  using OutTraits = TypeTraits<OutType>;

  CastOptions options;

  const int64_t length = static_cast<int64_t>(in_values.size());

  shared_ptr<Array> arr, expected;
  ArrayFromVector<InType, InT>(in_type, in_values, &arr);
  ArrayFromVector<OutType, OutT>(out_type, out_values, &expected);

  shared_ptr<Buffer> out_buffer;
  ASSERT_OK(ctx->Allocate(OutTraits::bytes_required(length), &out_buffer));

  std::unique_ptr<UnaryKernel> kernel;
  ASSERT_OK(GetCastFunction(*in_type, out_type, options, &kernel));

  const int64_t first_half = length / 2;

  auto out_data = ArrayData::Make(out_type, length, {nullptr, out_buffer});
  auto out_second_data = out_data->Copy();
  out_second_data->offset = first_half;

  Datum out_first(out_data);
  Datum out_second(out_second_data);

  // Cast each bit
  ASSERT_OK(kernel->Call(ctx, arr->Slice(0, first_half), &out_first));
  ASSERT_OK(kernel->Call(ctx, arr->Slice(first_half), &out_second));

  shared_ptr<Array> result = MakeArray(out_data);

  ASSERT_ARRAYS_EQUAL(*expected, *result);
}

TEST_F(TestCast, OffsetOutputBuffer) {
  // ARROW-1735
  vector<int32_t> v1 = {0, 10000, 2000, 1000, 0};
  vector<int64_t> e1 = {0, 10000, 2000, 1000, 0};

  auto in_type = int32();
  auto out_type = int64();
  CheckOffsetOutputCase<Int32Type, int32_t, Int64Type, int64_t>(&this->ctx_, in_type, v1,
                                                                out_type, e1);

  vector<bool> e2 = {false, true, true, true, false};

  out_type = boolean();
  CheckOffsetOutputCase<Int32Type, int32_t, BooleanType, bool>(&this->ctx_, in_type, v1,
                                                               boolean(), e2);

  vector<int16_t> e3 = {0, 10000, 2000, 1000, 0};
  CheckOffsetOutputCase<Int32Type, int32_t, Int16Type, int16_t>(&this->ctx_, in_type, v1,
                                                                int16(), e3);
}

TEST_F(TestCast, StringToBoolean) {
  CastOptions options;

  vector<bool> is_valid = {true, false, true, true, true};

  vector<std::string> v1 = {"False", "true", "true", "True", "false"};
  vector<std::string> v2 = {"0", "1", "1", "1", "0"};
  vector<bool> e = {false, true, true, true, false};
  CheckCase<StringType, std::string, BooleanType, bool>(utf8(), v1, is_valid, boolean(),
                                                        e, options);
  CheckCase<StringType, std::string, BooleanType, bool>(utf8(), v2, is_valid, boolean(),
                                                        e, options);
}

TEST_F(TestCast, StringToBooleanErrors) {
  CastOptions options;

  vector<bool> is_valid = {true};

  CheckFails<StringType, std::string>(utf8(), {"false "}, is_valid, boolean(), options);
  CheckFails<StringType, std::string>(utf8(), {"T"}, is_valid, boolean(), options);
}

TEST_F(TestCast, StringToNumber) {
  CastOptions options;

  vector<bool> is_valid = {true, false, true, true, true};

  // string to int
  vector<std::string> v_int = {"0", "1", "127", "-1", "0"};
  vector<int8_t> e_int8 = {0, 1, 127, -1, 0};
  vector<int16_t> e_int16 = {0, 1, 127, -1, 0};
  vector<int32_t> e_int32 = {0, 1, 127, -1, 0};
  vector<int64_t> e_int64 = {0, 1, 127, -1, 0};
  CheckCase<StringType, std::string, Int8Type, int8_t>(utf8(), v_int, is_valid, int8(),
                                                       e_int8, options);
  CheckCase<StringType, std::string, Int16Type, int16_t>(utf8(), v_int, is_valid, int16(),
                                                         e_int16, options);
  CheckCase<StringType, std::string, Int32Type, int32_t>(utf8(), v_int, is_valid, int32(),
                                                         e_int32, options);
  CheckCase<StringType, std::string, Int64Type, int64_t>(utf8(), v_int, is_valid, int64(),
                                                         e_int64, options);

  v_int = {"2147483647", "0", "-2147483648", "0", "0"};
  e_int32 = {2147483647, 0, -2147483648LL, 0, 0};
  CheckCase<StringType, std::string, Int32Type, int32_t>(utf8(), v_int, is_valid, int32(),
                                                         e_int32, options);
  v_int = {"9223372036854775807", "0", "-9223372036854775808", "0", "0"};
  e_int64 = {9223372036854775807LL, 0, (-9223372036854775807LL - 1), 0, 0};
  CheckCase<StringType, std::string, Int64Type, int64_t>(utf8(), v_int, is_valid, int64(),
                                                         e_int64, options);

  // string to uint
  vector<std::string> v_uint = {"0", "1", "127", "255", "0"};
  vector<uint8_t> e_uint8 = {0, 1, 127, 255, 0};
  vector<uint16_t> e_uint16 = {0, 1, 127, 255, 0};
  vector<uint32_t> e_uint32 = {0, 1, 127, 255, 0};
  vector<uint64_t> e_uint64 = {0, 1, 127, 255, 0};
  CheckCase<StringType, std::string, UInt8Type, uint8_t>(utf8(), v_uint, is_valid,
                                                         uint8(), e_uint8, options);
  CheckCase<StringType, std::string, UInt16Type, uint16_t>(utf8(), v_uint, is_valid,
                                                           uint16(), e_uint16, options);
  CheckCase<StringType, std::string, UInt32Type, uint32_t>(utf8(), v_uint, is_valid,
                                                           uint32(), e_uint32, options);
  CheckCase<StringType, std::string, UInt64Type, uint64_t>(utf8(), v_uint, is_valid,
                                                           uint64(), e_uint64, options);

  v_uint = {"4294967295", "0", "0", "0", "0"};
  e_uint32 = {4294967295, 0, 0, 0, 0};
  CheckCase<StringType, std::string, UInt32Type, uint32_t>(utf8(), v_uint, is_valid,
                                                           uint32(), e_uint32, options);
  v_uint = {"18446744073709551615", "0", "0", "0", "0"};
  e_uint64 = {18446744073709551615ULL, 0, 0, 0, 0};
  CheckCase<StringType, std::string, UInt64Type, uint64_t>(utf8(), v_uint, is_valid,
                                                           uint64(), e_uint64, options);

  // string to float
  vector<std::string> v_float = {"0.1", "1.2", "127.3", "200.4", "0.5"};
  vector<float> e_float = {0.1f, 1.2f, 127.3f, 200.4f, 0.5f};
  vector<double> e_double = {0.1, 1.2, 127.3, 200.4, 0.5};
  CheckCase<StringType, std::string, FloatType, float>(utf8(), v_float, is_valid,
                                                       float32(), e_float, options);
  CheckCase<StringType, std::string, DoubleType, double>(utf8(), v_float, is_valid,
                                                         float64(), e_double, options);

  // Test that casting is locale-independent
  auto global_locale = std::locale();
  try {
    // French locale uses the comma as decimal point
    std::locale::global(std::locale("fr_FR.UTF-8"));
  } catch (std::runtime_error&) {
    // Locale unavailable, ignore
  }
  CheckCase<StringType, std::string, FloatType, float>(utf8(), v_float, is_valid,
                                                       float32(), e_float, options);
  CheckCase<StringType, std::string, DoubleType, double>(utf8(), v_float, is_valid,
                                                         float64(), e_double, options);
  std::locale::global(global_locale);
}

TEST_F(TestCast, StringToNumberErrors) {
  CastOptions options;

  vector<bool> is_valid = {true};

  CheckFails<StringType, std::string>(utf8(), {"z"}, is_valid, int8(), options);
  CheckFails<StringType, std::string>(utf8(), {"12 z"}, is_valid, int8(), options);
  CheckFails<StringType, std::string>(utf8(), {"128"}, is_valid, int8(), options);
  CheckFails<StringType, std::string>(utf8(), {"-129"}, is_valid, int8(), options);
  CheckFails<StringType, std::string>(utf8(), {"0.5"}, is_valid, int8(), options);

  CheckFails<StringType, std::string>(utf8(), {"256"}, is_valid, uint8(), options);
  CheckFails<StringType, std::string>(utf8(), {"-1"}, is_valid, uint8(), options);

  CheckFails<StringType, std::string>(utf8(), {"z"}, is_valid, float32(), options);
}

TEST_F(TestCast, StringToTimestamp) {
  CastOptions options;

  vector<bool> is_valid = {true, false, true};
  vector<std::string> strings = {"1970-01-01", "xxx", "2000-02-29"};

  auto type = timestamp(TimeUnit::SECOND);
  vector<int64_t> e = {0, 0, 951782400};
  CheckCase<StringType, std::string, TimestampType, int64_t>(utf8(), strings, is_valid,
                                                             type, e, options);

  type = timestamp(TimeUnit::MICRO);
  e = {0, 0, 951782400000000LL};
  CheckCase<StringType, std::string, TimestampType, int64_t>(utf8(), strings, is_valid,
                                                             type, e, options);

  // NOTE: timestamp parsing is tested comprehensively in parsing-util-test.cc
}

TEST_F(TestCast, StringToTimestampErrors) {
  CastOptions options;

  vector<bool> is_valid = {true};

  for (auto unit : {TimeUnit::SECOND, TimeUnit::MILLI, TimeUnit::MICRO, TimeUnit::NANO}) {
    auto type = timestamp(unit);
    CheckFails<StringType, std::string>(utf8(), {""}, is_valid, type, options);
    CheckFails<StringType, std::string>(utf8(), {"xxx"}, is_valid, type, options);
  }
}

constexpr const char* kInvalidUtf8 = "\xa0\xa1";

TEST_F(TestCast, BinaryToString) {
  CastOptions options;

  // All valid except the last one
  vector<bool> all = {1, 1, 1, 1, 1};
  vector<bool> valid = {1, 1, 1, 1, 0};
  vector<std::string> strings = {"Hi", "olá mundo", "你好世界", "", kInvalidUtf8};

  std::shared_ptr<Array> array;

  // Should accept when invalid but null.
  ArrayFromVector<BinaryType, std::string>(binary(), valid, strings, &array);
  CheckZeroCopy(*array, utf8());

  // Should refuse due to invalid utf8 payload
  CheckFails<BinaryType, std::string>(binary(), strings, all, utf8(), options);

  // Should accept due to option override
  options.allow_invalid_utf8 = true;
  CheckCase<BinaryType, std::string, StringType, std::string>(binary(), strings, all,
                                                              utf8(), strings, options);
}

template <typename TestType>
class TestDictionaryCast : public TestCast {};

typedef ::testing::Types<NullType, UInt8Type, Int8Type, UInt16Type, Int16Type, Int32Type,
                         UInt32Type, UInt64Type, Int64Type, FloatType, DoubleType,
                         Date32Type, Date64Type, FixedSizeBinaryType, BinaryType>
    TestTypes;

TYPED_TEST_CASE(TestDictionaryCast, TestTypes);

TYPED_TEST(TestDictionaryCast, Basic) {
  CastOptions options;
  shared_ptr<Array> plain_array =
      TestBase::MakeRandomArray<typename TypeTraits<TypeParam>::ArrayType>(10, 2);

  Datum out;
  ASSERT_OK(DictionaryEncode(&this->ctx_, plain_array->data(), &out));

  this->CheckPass(*MakeArray(out.array()), *plain_array, plain_array->type(), options);
}

TEST_F(TestCast, DictToNonDictNoNulls) {
  vector<std::string> dict_values = {"foo", "bar", "baz"};
  auto ex_dict = _MakeArray<StringType, std::string>(utf8(), dict_values, {});
  auto dict_type = dictionary(int32(), ex_dict);

  // Explicitly construct with nullptr for the null_bitmap_data
  std::vector<int32_t> i1 = {1, 0, 1};
  std::vector<int32_t> i2 = {2, 1, 0, 1};
  auto c1 = std::make_shared<NumericArray<Int32Type>>(3, Buffer::Wrap(i1));
  auto c2 = std::make_shared<NumericArray<Int32Type>>(4, Buffer::Wrap(i2));

  ArrayVector dict_arrays = {std::make_shared<DictionaryArray>(dict_type, c1),
                             std::make_shared<DictionaryArray>(dict_type, c2)};
  auto dict_carr = std::make_shared<ChunkedArray>(dict_arrays);

  Datum cast_input(dict_carr);
  Datum cast_output;
  // Ensure that casting works even when the null_bitmap_data array is a nullptr
  ASSERT_OK(Cast(&this->ctx_, cast_input,
                 static_cast<DictionaryType&>(*dict_type).dictionary()->type(),
                 CastOptions(), &cast_output));
  ASSERT_EQ(Datum::CHUNKED_ARRAY, cast_output.kind());

  auto e1 = _MakeArray<StringType, std::string>(utf8(), {"bar", "foo", "bar"}, {});
  auto e2 = _MakeArray<StringType, std::string>(utf8(), {"baz", "bar", "foo", "bar"}, {});

  auto chunks = cast_output.chunked_array()->chunks();
  ASSERT_EQ(chunks.size(), 2);
  ASSERT_ARRAYS_EQUAL(*e1, *chunks[0]);
  ASSERT_ARRAYS_EQUAL(*e2, *chunks[1]);
}

/*TYPED_TEST(TestDictionaryCast, Reverse) {
  CastOptions options;
  shared_ptr<Array> plain_array =
      TestBase::MakeRandomArray<typename TypeTraits<TypeParam>::ArrayType>(10, 2);

  shared_ptr<Array> dict_array;
  ASSERT_OK(EncodeArrayToDictionary(*plain_array, this->pool_, &dict_array));

  this->CheckPass(*plain_array, *dict_array, dict_array->type(), options);
}*/

TEST_F(TestCast, ListToList) {
  CastOptions options;
  std::shared_ptr<Array> offsets;

  vector<int32_t> offsets_values = {0, 1, 2, 5, 7, 7, 8, 10};
  std::vector<bool> offsets_is_valid = {true, true, true, true, false, true, true, true};
  ArrayFromVector<Int32Type, int32_t>(offsets_is_valid, offsets_values, &offsets);

  shared_ptr<Array> int32_plain_array =
      TestBase::MakeRandomArray<typename TypeTraits<Int32Type>::ArrayType>(10, 2);
  std::shared_ptr<Array> int32_list_array;
  ASSERT_OK(
      ListArray::FromArrays(*offsets, *int32_plain_array, pool_, &int32_list_array));

  std::shared_ptr<Array> int64_plain_array;
  ASSERT_OK(Cast(&this->ctx_, *int32_plain_array, int64(), options, &int64_plain_array));
  std::shared_ptr<Array> int64_list_array;
  ASSERT_OK(
      ListArray::FromArrays(*offsets, *int64_plain_array, pool_, &int64_list_array));

  std::shared_ptr<Array> float64_plain_array;
  ASSERT_OK(
      Cast(&this->ctx_, *int32_plain_array, float64(), options, &float64_plain_array));
  std::shared_ptr<Array> float64_list_array;
  ASSERT_OK(
      ListArray::FromArrays(*offsets, *float64_plain_array, pool_, &float64_list_array));

  CheckPass(*int32_list_array, *int64_list_array, int64_list_array->type(), options);
  CheckPass(*int32_list_array, *float64_list_array, float64_list_array->type(), options);
  CheckPass(*int64_list_array, *int32_list_array, int32_list_array->type(), options);
  CheckPass(*int64_list_array, *float64_list_array, float64_list_array->type(), options);

  options.allow_float_truncate = true;
  CheckPass(*float64_list_array, *int32_list_array, int32_list_array->type(), options);
  CheckPass(*float64_list_array, *int64_list_array, int64_list_array->type(), options);
}

TEST_F(TestCast, IdentityCasts) {
  // ARROW-4102
  auto CheckIdentityCast = [this](std::shared_ptr<DataType> type,
                                  const std::string& json) {
    auto arr = ArrayFromJSON(type, json);
    CheckZeroCopy(*arr, type);
  };

  CheckIdentityCast(null(), "[null, null, null]");
  CheckIdentityCast(boolean(), "[false, true, null, false]");

  for (auto type : kNumericTypes) {
    CheckIdentityCast(type, "[1, 2, null, 4]");
  }
  CheckIdentityCast(binary(), "[\"foo\", \"bar\"]");
  CheckIdentityCast(utf8(), "[\"foo\", \"bar\"]");
  CheckIdentityCast(fixed_size_binary(3), "[\"foo\", \"bar\"]");

  CheckIdentityCast(list(int8()), "[[1, 2], [null], [], [3]]");

  CheckIdentityCast(time32(TimeUnit::MILLI), "[1, 2, 3, 4]");
  CheckIdentityCast(time64(TimeUnit::MICRO), "[1, 2, 3, 4]");
  CheckIdentityCast(date32(), "[1, 2, 3, 4]");
  CheckIdentityCast(date64(), "[86400000, 0]");
  CheckIdentityCast(timestamp(TimeUnit::SECOND), "[1, 2, 3, 4]");

  {
    auto dict_type = dictionary(int8(), ArrayFromJSON(int8(), "[1, 2, 3]"));
    auto dict_indices = ArrayFromJSON(int8(), "[0, 1, 2, 0, null, 2]");
    auto dict_array = std::make_shared<DictionaryArray>(dict_type, dict_indices);
    CheckZeroCopy(*dict_array, dict_type);
  }
}

}  // namespace compute
}  // namespace arrow
