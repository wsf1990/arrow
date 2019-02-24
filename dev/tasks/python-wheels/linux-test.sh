#!/bin/bash

# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

# Install built wheel
pip install /arrow/python/manylinux1/dist/*.whl

# Runs tests on installed distribution from an empty directory
python --version

# Test optional dependencies
python -c "
import sys
import pyarrow
import pyarrow.orc
import pyarrow.parquet
import pyarrow.plasma
import tensorflow

if sys.version_info.major > 2:
    import pyarrow.gandiva
"

# Run pyarrow tests
pip install -r /arrow/python/requirements-test.txt
pytest --pyargs pyarrow
