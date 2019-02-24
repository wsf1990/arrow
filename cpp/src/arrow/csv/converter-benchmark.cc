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

#include "benchmark/benchmark.h"

#include <sstream>
#include <string>

#include "arrow/csv/converter.h"
#include "arrow/csv/options.h"
#include "arrow/csv/parser.h"
#include "arrow/csv/test-common.h"
#include "arrow/testing/gtest_util.h"

namespace arrow {
namespace csv {

static std::shared_ptr<BlockParser> BuildInt64Data(int32_t num_rows) {
  const std::vector<std::string> base_rows = {"123\n", "4\n",   "-317005557\n",
                                              "\n",    "N/A\n", "0\n"};
  std::vector<std::string> rows;
  for (int32_t i = 0; i < num_rows; ++i) {
    rows.push_back(base_rows[i % base_rows.size()]);
  }

  std::shared_ptr<BlockParser> result;
  MakeCSVParser(rows, &result);
  return result;
}

static std::shared_ptr<BlockParser> BuildFloatData(int32_t num_rows) {
  const std::vector<std::string> base_rows = {"0\n", "123.456\n", "-3170.55766\n", "\n",
                                              "N/A\n"};
  std::vector<std::string> rows;
  for (int32_t i = 0; i < num_rows; ++i) {
    rows.push_back(base_rows[i % base_rows.size()]);
  }

  std::shared_ptr<BlockParser> result;
  MakeCSVParser(rows, &result);
  return result;
}

static void BenchmarkConversion(benchmark::State& state,  // NOLINT non-const reference
                                BlockParser& parser,
                                const std::shared_ptr<DataType>& type,
                                ConvertOptions options) {
  std::shared_ptr<Converter> converter;
  ABORT_NOT_OK(Converter::Make(type, options, &converter));

  while (state.KeepRunning()) {
    std::shared_ptr<Array> result;
    ABORT_NOT_OK(converter->Convert(parser, 0 /* col_index */, &result));
    if (result->length() != parser.num_rows()) {
      std::cerr << "Conversion incomplete\n";
      std::abort();
    }
  }

  state.SetItemsProcessed(state.iterations() * parser.num_rows());
}

static void BM_Int64Conversion(benchmark::State& state) {  // NOLINT non-const reference
  const int32_t num_rows = 10000;
  auto parser = BuildInt64Data(num_rows);
  auto options = ConvertOptions::Defaults();

  BenchmarkConversion(state, *parser, int64(), options);
}

static void BM_FloatConversion(benchmark::State& state) {  // NOLINT non-const reference
  const int32_t num_rows = 10000;
  auto parser = BuildFloatData(num_rows);
  auto options = ConvertOptions::Defaults();

  BenchmarkConversion(state, *parser, float64(), options);
}

BENCHMARK(BM_Int64Conversion)->Repetitions(3);
BENCHMARK(BM_FloatConversion)->Repetitions(3);

}  // namespace csv
}  // namespace arrow
