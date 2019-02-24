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

#ifndef ARROW_COMPUTE_KERNELS_SUM_H
#define ARROW_COMPUTE_KERNELS_SUM_H

#include <memory>
#include <type_traits>

#include "arrow/status.h"
#include "arrow/type.h"
#include "arrow/type_traits.h"
#include "arrow/util/visibility.h"

namespace arrow {

class Array;
class DataType;

namespace compute {

// Find the largest compatible primitive type for a primitive type.
template <typename I, typename Enable = void>
struct FindAccumulatorType {
  using Type = DoubleType;
};

template <typename I>
struct FindAccumulatorType<I, typename std::enable_if<IsSignedInt<I>::value>::type> {
  using Type = Int64Type;
};

template <typename I>
struct FindAccumulatorType<I, typename std::enable_if<IsUnsignedInt<I>::value>::type> {
  using Type = UInt64Type;
};

template <typename I>
struct FindAccumulatorType<I, typename std::enable_if<IsFloatingPoint<I>::value>::type> {
  using Type = DoubleType;
};

struct Datum;
class FunctionContext;
class AggregateFunction;

ARROW_EXPORT
std::shared_ptr<AggregateFunction> MakeSumAggregateFunction(const DataType& type,
                                                            FunctionContext* context);

/// \brief Sum values of a numeric array.
///
/// \param[in] context the FunctionContext
/// \param[in] value datum to sum, expecting Array or ChunkedArray
/// \param[out] out resulting datum
///
/// \since 0.13.0
/// \note API not yet finalized
ARROW_EXPORT
Status Sum(FunctionContext* context, const Datum& value, Datum* out);

/// \brief Sum values of a numeric array.
///
/// \param[in] context the FunctionContext
/// \param[in] array to sum
/// \param[out] out resulting datum
///
/// \since 0.13.0
/// \note API not yet finalized
ARROW_EXPORT
Status Sum(FunctionContext* context, const Array& array, Datum* out);

}  // namespace compute
}  // namespace arrow

#endif  // ARROW_COMPUTE_KERNELS_CAST_H
