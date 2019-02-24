﻿// Licensed to the Apache Software Foundation (ASF) under one or more
// contributor license agreements. See the NOTICE file distributed with
// this work for additional information regarding copyright ownership.
// The ASF licenses this file to You under the Apache License, Version 2.0
// (the "License"); you may not use this file except in compliance with
// the License.  You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

using System;
using System.Collections.Generic;
using System.Linq;

namespace Apache.Arrow
{
    public class RecordBatch
    {
        public Schema Schema { get; }
        public int ColumnCount => _arrays.Count;
        public IEnumerable<IArrowArray> Arrays => _arrays;
        public int Length { get; }

        private readonly IList<IArrowArray> _arrays;

        public IArrowArray Column(int i)
        {
            return _arrays[i];
        }

        public IArrowArray Column(string columnName)
        {
            var fieldIndex = Schema.GetFieldIndex(columnName);
            return _arrays[fieldIndex];
        }

        public RecordBatch(Schema schema, IEnumerable<IArrowArray> data, int length)
        {
            if (length < 0)
            {
                throw new ArgumentOutOfRangeException(nameof(length));
            }

            _arrays = data?.ToList() ?? throw new ArgumentNullException(nameof(data));

            Schema = schema ?? throw new ArgumentNullException(nameof(schema));
            Length = length;
        }
    }
}
