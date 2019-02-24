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

set -e

source arrow/ci/travis_env_common.sh

CPP_BUILD_DIR=$TRAVIS_BUILD_DIR/cpp/build/release

pushd arrow/java
  if [ $TRAVIS_OS_NAME == "linux" ]; then
    ldd $CPP_BUILD_DIR/libgandiva_jni.so
  fi

  # build the entire project
  mvn clean install -DskipTests -P gandiva -Dgandiva.cpp.build.dir=$CPP_BUILD_DIR
  # test only gandiva
  mvn test -P gandiva -pl gandiva -Dgandiva.cpp.build.dir=$CPP_BUILD_DIR
  # copy the jars to distribution folder
  find gandiva/target/ -name "*.jar" -not -name "*tests*" -exec cp  {} ../../dist/ \;
popd
