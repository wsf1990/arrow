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

// Alogrithms adapted from Apache Impala

#include "gandiva/precompiled/decimal_ops.h"

#include <algorithm>

#include "gandiva/decimal_type_util.h"
#include "gandiva/logging.h"

namespace gandiva {
namespace decimalops {

using arrow::BasicDecimal128;

static BasicDecimal128 CheckAndIncreaseScale(const BasicDecimal128& in, int32_t delta) {
  return (delta <= 0) ? in : in.IncreaseScaleBy(delta);
}

static BasicDecimal128 CheckAndReduceScale(const BasicDecimal128& in, int32_t delta) {
  return (delta <= 0) ? in : in.ReduceScaleBy(delta);
}

/// Adjust x and y to the same scale, and add them.
static BasicDecimal128 AddFastPath(const BasicDecimalScalar128& x,
                                   const BasicDecimalScalar128& y, int32_t out_scale) {
  auto higher_scale = std::max(x.scale(), y.scale());

  auto x_scaled = CheckAndIncreaseScale(x.value(), higher_scale - x.scale());
  auto y_scaled = CheckAndIncreaseScale(y.value(), higher_scale - y.scale());
  return x_scaled + y_scaled;
}

/// Add x and y, caller has ensured there can be no overflow.
static BasicDecimal128 AddNoOverflow(const BasicDecimalScalar128& x,
                                     const BasicDecimalScalar128& y, int32_t out_scale) {
  auto higher_scale = std::max(x.scale(), y.scale());
  auto sum = AddFastPath(x, y, out_scale);
  return CheckAndReduceScale(sum, higher_scale - out_scale);
}

/// Both x_value and y_value must be >= 0
static BasicDecimal128 AddLargePositive(const BasicDecimalScalar128& x,
                                        const BasicDecimalScalar128& y,
                                        int32_t out_scale) {
  DCHECK_GE(x.value(), 0);
  DCHECK_GE(y.value(), 0);

  // separate out whole/fractions.
  BasicDecimal128 x_left, x_right, y_left, y_right;
  x.value().GetWholeAndFraction(x.scale(), &x_left, &x_right);
  y.value().GetWholeAndFraction(y.scale(), &y_left, &y_right);

  // Adjust fractional parts to higher scale.
  auto higher_scale = std::max(x.scale(), y.scale());
  auto x_right_scaled = CheckAndIncreaseScale(x_right, higher_scale - x.scale());
  auto y_right_scaled = CheckAndIncreaseScale(y_right, higher_scale - y.scale());

  BasicDecimal128 right;
  BasicDecimal128 carry_to_left;
  auto multiplier = BasicDecimal128::GetScaleMultiplier(higher_scale);
  if (x_right_scaled >= multiplier - y_right_scaled) {
    right = x_right_scaled - (multiplier - y_right_scaled);
    carry_to_left = 1;
  } else {
    right = x_right_scaled + y_right_scaled;
    carry_to_left = 0;
  }
  right = CheckAndReduceScale(right, higher_scale - out_scale);

  auto left = x_left + y_left + carry_to_left;
  return (left * BasicDecimal128::GetScaleMultiplier(out_scale)) + right;
}

/// x_value and y_value cannot be 0, and one must be positive and the other negative.
static BasicDecimal128 AddLargeNegative(const BasicDecimalScalar128& x,
                                        const BasicDecimalScalar128& y,
                                        int32_t out_scale) {
  DCHECK_NE(x.value(), 0);
  DCHECK_NE(y.value(), 0);
  DCHECK((x.value() < 0 && y.value() > 0) || (x.value() > 0 && y.value() < 0));

  // separate out whole/fractions.
  BasicDecimal128 x_left, x_right, y_left, y_right;
  x.value().GetWholeAndFraction(x.scale(), &x_left, &x_right);
  y.value().GetWholeAndFraction(y.scale(), &y_left, &y_right);

  // Adjust fractional parts to higher scale.
  auto higher_scale = std::max(x.scale(), y.scale());
  x_right = CheckAndIncreaseScale(x_right, higher_scale - x.scale());
  y_right = CheckAndIncreaseScale(y_right, higher_scale - y.scale());

  // Overflow not possible because one is +ve and the other is -ve.
  auto left = x_left + y_left;
  auto right = x_right + y_right;

  // If the whole and fractional parts have different signs, then we need to make the
  // fractional part have the same sign as the whole part. If either left or right is
  // zero, then nothing needs to be done.
  if (left < 0 && right > 0) {
    left += 1;
    right -= BasicDecimal128::GetScaleMultiplier(higher_scale);
  } else if (left > 0 && right < 0) {
    left -= 1;
    right += BasicDecimal128::GetScaleMultiplier(higher_scale);
  }
  right = CheckAndReduceScale(right, higher_scale - out_scale);
  return (left * BasicDecimal128::GetScaleMultiplier(out_scale)) + right;
}

static BasicDecimal128 AddLarge(const BasicDecimalScalar128& x,
                                const BasicDecimalScalar128& y, int32_t out_scale) {
  if (x.value() >= 0 && y.value() >= 0) {
    // both positive or 0
    return AddLargePositive(x, y, out_scale);
  } else if (x.value() <= 0 && y.value() <= 0) {
    // both negative or 0
    BasicDecimalScalar128 x_neg(-x.value(), x.precision(), x.scale());
    BasicDecimalScalar128 y_neg(-y.value(), y.precision(), y.scale());
    return -AddLargePositive(x_neg, y_neg, out_scale);
  } else {
    // one positive and the other negative
    return AddLargeNegative(x, y, out_scale);
  }
}

// Suppose we have a number that requires x bits to be represented and we scale it up by
// 10^scale_by. Let's say now y bits are required to represent it. This function returns
// the maximum possible y - x for a given 'scale_by'.
inline int32_t MaxBitsRequiredIncreaseAfterScaling(int32_t scale_by) {
  // We rely on the following formula:
  // bits_required(x * 10^y) <= bits_required(x) + floor(log2(10^y)) + 1
  // We precompute floor(log2(10^x)) + 1 for x = 0, 1, 2...75, 76
  DCHECK_GE(scale_by, 0);
  DCHECK_LE(scale_by, 76);
  static const int32_t floor_log2_plus_one[] = {
      0,   4,   7,   10,  14,  17,  20,  24,  27,  30,  34,  37,  40,  44,  47,  50,
      54,  57,  60,  64,  67,  70,  74,  77,  80,  84,  87,  90,  94,  97,  100, 103,
      107, 110, 113, 117, 120, 123, 127, 130, 133, 137, 140, 143, 147, 150, 153, 157,
      160, 163, 167, 170, 173, 177, 180, 183, 187, 190, 193, 196, 200, 203, 206, 210,
      213, 216, 220, 223, 226, 230, 233, 236, 240, 243, 246, 250, 253};
  return floor_log2_plus_one[scale_by];
}

// If we have a number with 'num_lz' leading zeros, and we scale it up by 10^scale_by,
// this function returns the minimum number of leading zeros the result can have.
inline int32_t MinLeadingZerosAfterScaling(int32_t num_lz, int32_t scale_by) {
  DCHECK_GE(scale_by, 0);
  DCHECK_LE(scale_by, 76);
  int32_t result = num_lz - MaxBitsRequiredIncreaseAfterScaling(scale_by);
  return result;
}

// Returns the maximum possible number of bits required to represent num * 10^scale_by.
inline int32_t MaxBitsRequiredAfterScaling(const BasicDecimalScalar128& num,
                                           int32_t scale_by) {
  auto value = num.value();
  auto value_abs = value.Abs();

  int32_t num_occupied = 128 - value_abs.CountLeadingBinaryZeros();
  DCHECK_GE(scale_by, 0);
  DCHECK_LE(scale_by, 76);
  return num_occupied + MaxBitsRequiredIncreaseAfterScaling(scale_by);
}

// Returns the minimum number of leading zero x or y would have after one of them gets
// scaled up to match the scale of the other one.
inline int32_t MinLeadingZeros(const BasicDecimalScalar128& x,
                               const BasicDecimalScalar128& y) {
  auto x_value = x.value();
  auto x_value_abs = x_value.Abs();

  auto y_value = y.value();
  auto y_value_abs = y_value.Abs();

  int32_t x_lz = x_value_abs.CountLeadingBinaryZeros();
  int32_t y_lz = y_value_abs.CountLeadingBinaryZeros();
  if (x.scale() < y.scale()) {
    x_lz = MinLeadingZerosAfterScaling(x_lz, y.scale() - x.scale());
  } else if (x.scale() > y.scale()) {
    y_lz = MinLeadingZerosAfterScaling(y_lz, x.scale() - y.scale());
  }
  return std::min(x_lz, y_lz);
}

BasicDecimal128 Add(const BasicDecimalScalar128& x, const BasicDecimalScalar128& y,
                    int32_t out_precision, int32_t out_scale) {
  if (out_precision < DecimalTypeUtil::kMaxPrecision) {
    // fast-path add
    return AddFastPath(x, y, out_scale);
  } else {
    int32_t min_lz = MinLeadingZeros(x, y);
    if (min_lz >= 3) {
      // If both numbers have at least MIN_LZ leading zeros, we can add them directly
      // without the risk of overflow.
      // We want the result to have at least 2 leading zeros, which ensures that it fits
      // into the maximum decimal because 2^126 - 1 < 10^38 - 1. If both x and y have at
      // least 3 leading zeros, then we are guaranteed that the result will have at lest 2
      // leading zeros.
      return AddNoOverflow(x, y, out_scale);
    } else {
      // slower-version : add whole/fraction parts separately, and then, combine.
      return AddLarge(x, y, out_scale);
    }
  }
}

BasicDecimal128 Subtract(const BasicDecimalScalar128& x, const BasicDecimalScalar128& y,
                         int32_t out_precision, int32_t out_scale) {
  return Add(x, {-y.value(), y.precision(), y.scale()}, out_precision, out_scale);
}

}  // namespace decimalops
}  // namespace gandiva
