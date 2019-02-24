#!/usr/bin/env bash

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

set -ex

# Disable toolchain variables in this script
export ARROW_TRAVIS_USE_TOOLCHAIN=0
source $TRAVIS_BUILD_DIR/ci/travis_env_common.sh

# CMake formatting check
pip install cmake_format
$TRAVIS_BUILD_DIR/run-cmake-format.py --check

# C++ code linting
if [ "$ARROW_CI_CPP_AFFECTED" != "0" ]; then
  mkdir $ARROW_CPP_DIR/lint
  pushd $ARROW_CPP_DIR/lint

  cmake .. -DARROW_ONLY_LINT=ON
  make lint
  make check-format

  python $ARROW_CPP_DIR/build-support/lint_cpp_cli.py $ARROW_CPP_DIR/src

  popd
fi

# Python style checks
# (need Python 3 for crossbow)
FLAKE8="python3 -m flake8"
python3 -m pip install -q flake8

if [ "$ARROW_CI_DEV_AFFECTED" != "0" ]; then
  $FLAKE8 --count $ARROW_DEV_DIR
fi

if [ "$ARROW_CI_INTEGRATION_AFFECTED" != "0" ]; then
  $FLAKE8 --count $ARROW_INTEGRATION_DIR
fi

if [ "$ARROW_CI_PYTHON_AFFECTED" != "0" ]; then
  $FLAKE8 --count $ARROW_PYTHON_DIR
  # Check Cython files with some checks turned off
  $FLAKE8 --count \
          --config=$ARROW_PYTHON_DIR/.flake8.cython \
          $ARROW_PYTHON_DIR
fi

if [ "$ARROW_CI_R_AFFECTED" != "0" ]; then
  pushd $ARROW_R_DIR
  ./lint.sh
  popd
fi
