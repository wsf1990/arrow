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

# hide nodejs experimental-feature warnings
export NODE_NO_WARNINGS=1
export MINICONDA=$HOME/miniconda
export CONDA_PKGS_DIRS=$HOME/.conda_packages
export CONDA_BINUTILS_VERSION=2.31

export ARROW_LLVM_VERSION=7.0
export CONDA_LLVM_VERSION="7.0.*"

# extract the major version
export ARROW_LLVM_MAJOR_VERSION=$(echo $ARROW_LLVM_VERSION | cut -d. -f1)

export ARROW_CPP_DIR=$TRAVIS_BUILD_DIR/cpp
export ARROW_PYTHON_DIR=$TRAVIS_BUILD_DIR/python
export ARROW_C_GLIB_DIR=$TRAVIS_BUILD_DIR/c_glib
export ARROW_JAVA_DIR=${TRAVIS_BUILD_DIR}/java
export ARROW_JS_DIR=${TRAVIS_BUILD_DIR}/js
export ARROW_INTEGRATION_DIR=$TRAVIS_BUILD_DIR/integration
export ARROW_DEV_DIR=$TRAVIS_BUILD_DIR/dev
export ARROW_CROSSBOW_DIR=$TRAVIS_BUILD_DIR/dev/tasks
export ARROW_RUBY_DIR=$TRAVIS_BUILD_DIR/ruby
export ARROW_RUST_DIR=${TRAVIS_BUILD_DIR}/rust
export ARROW_R_DIR=${TRAVIS_BUILD_DIR}/r

export ARROW_TRAVIS_COVERAGE=${ARROW_TRAVIS_COVERAGE:=0}

if [ "$ARROW_TRAVIS_COVERAGE" == "1" ]; then
    export ARROW_CPP_COVERAGE_FILE=${TRAVIS_BUILD_DIR}/coverage.info
    export ARROW_PYTHON_COVERAGE_FILE=${TRAVIS_BUILD_DIR}/.coverage
fi

export CPP_BUILD_DIR=$TRAVIS_BUILD_DIR/cpp-build

export ARROW_CPP_INSTALL=$TRAVIS_BUILD_DIR/cpp-install
export ARROW_CPP_BUILD_DIR=$TRAVIS_BUILD_DIR/cpp-build
export ARROW_C_GLIB_INSTALL_AUTOTOOLS=$TRAVIS_BUILD_DIR/c-glib-install-autotools
export ARROW_C_GLIB_INSTALL_MESON=$TRAVIS_BUILD_DIR/c-glib-install-meson

export CMAKE_EXPORT_COMPILE_COMMANDS=1

export ARROW_BUILD_TYPE=${ARROW_BUILD_TYPE:=debug}
export ARROW_BUILD_WARNING_LEVEL=${ARROW_BUILD_WARNING_LEVEL:=Production}

if [ "$ARROW_TRAVIS_USE_TOOLCHAIN" == "1" ]; then
  # C++ toolchain
  export CPP_TOOLCHAIN=$TRAVIS_BUILD_DIR/cpp-toolchain
  export ARROW_BUILD_TOOLCHAIN=$CPP_TOOLCHAIN
  export BOOST_ROOT=$CPP_TOOLCHAIN

  # Protocol buffers used by Apache ORC thirdparty build
  export PROTOBUF_HOME=$CPP_TOOLCHAIN

  export PATH=$CPP_TOOLCHAIN/bin:$PATH
  export LD_LIBRARY_PATH=$CPP_TOOLCHAIN/lib:$LD_LIBRARY_PATH
  export TRAVIS_MAKE=ninja
else
  export TRAVIS_MAKE=make
fi

if [ $TRAVIS_OS_NAME == "osx" ]; then
  export GOPATH=$TRAVIS_BUILD_DIR/gopath
fi

export PARQUET_TEST_DATA=$TRAVIS_BUILD_DIR/cpp/submodules/parquet-testing/data

# e.g. "trusty" or "xenial"
if [ $TRAVIS_OS_NAME == "linux" ]; then
  export DISTRO_CODENAME=`lsb_release -s -c`
fi

if [ "$ARROW_TRAVIS_USE_SYSTEM_JAVA" == "1" ]; then
    # Use the Ubuntu-provided OpenJDK
    unset JAVA_HOME
    export TRAVIS_MVN=/usr/bin/mvn
    export TRAVIS_JAVA=/usr/bin/java
else
    export TRAVIS_MVN=mvn
    export TRAVIS_JAVA=java
fi
