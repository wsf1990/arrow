<!---
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing,
  software distributed under the License is distributed on an
  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
  KIND, either express or implied.  See the License for the
  specific language governing permissions and limitations
  under the License.
-->

# Developing Arrow C++ on Windows

## System setup, conda, and conda-forge

Since some of the Arrow developers work in the Python ecosystem, we are
investing time in maintaining the thirdparty build dependencies for Arrow and
related C++ libraries using the conda package manager. Others are free to add
other development instructions for Windows here.

### conda and package toolchain

[Miniconda][1] is a minimal Python distribution including the conda package
manager. To get started, download and install a 64-bit distribution.

We recommend using packages from [conda-forge][2].
Launch cmd.exe and run following commands:

```shell
conda config --add channels conda-forge
```

Now, you can bootstrap a build environment (call from the root directory of the
Arrow codebase):

```shell
conda create -n arrow-dev --file=ci\conda_env_cpp.yml
```

> **Note:** Make sure to get the `conda-forge` build of `gflags` as the
> naming of the library differs from that in the `defaults` channel.

Activate just created conda environment with pre-installed packages from
previous step:

```shell
activate arrow-dev
```

We are using the [cmake][4] tool to support Windows builds.
To allow cmake to pick up 3rd party dependencies, you should set
`ARROW_BUILD_TOOLCHAIN` environment variable to contain `Library` folder
path of new created on previous step `arrow-dev` conda environment.

To set `ARROW_BUILD_TOOLCHAIN` environment variable visible only for current terminal
session you can run following. `%CONDA_PREFIX` is set by conda to the current environment
root by the `activate` script.
```shell
set ARROW_BUILD_TOOLCHAIN=%CONDA_PREFIX%\Library
```

To validate value of `ARROW_BUILD_TOOLCHAIN` environment variable you can run following terminal command:
```shell
echo %ARROW_BUILD_TOOLCHAIN%
```

As alternative to `ARROW_BUILD_TOOLCHAIN`, it's possible to configure path
to each 3rd party dependency separately by setting appropriate environment
variable:

`FLATBUFFERS_HOME` variable with path to `flatbuffers` installation
`RAPIDJSON_HOME` variable with path to `rapidjson` installation
`GFLAGS_HOME` variable with path to `gflags` installation
`SNAPPY_HOME` variable with path to `snappy` installation
`ZLIB_HOME` variable with path to `zlib` installation
`BROTLI_HOME` variable with path to `brotli` installation
`LZ4_HOME` variable with path to `lz4` installation
`ZSTD_HOME` variable with path to `zstd` installation

### Customize static libraries names lookup of 3rd party dependencies

If you decided to use pre-built 3rd party dependencies libs, it's possible to
configure Arrow's cmake build script to search for customized names of 3rd
party static libs.

`brotli`. Set `BROTLI_HOME` environment variable. Pass
`-DBROTLI_MSVC_STATIC_LIB_SUFFIX=%BROTLI_SUFFIX%` to link with
brotli*%BROTLI_SUFFIX%.lib. For brotli versions <= 0.6.0 installed from
conda-forge this must be set to `_static`, otherwise the default of `-static`
is used.

`snappy`. Set `SNAPPY_HOME` environment variable. Pass
`-DSNAPPY_MSVC_STATIC_LIB_SUFFIX=%SNAPPY_SUFFIX%` to link with
snappy%SNAPPY_SUFFIX%.lib.

`lz4`. Set `LZ4_HOME` environment variable. Pass
`-LZ4_MSVC_STATIC_LIB_SUFFIX=%LZ4_SUFFIX%` to link with
lz4%LZ4_SUFFIX%.lib.

`zstd`. Set `ZSTD_HOME` environment variable. Pass
`-ZSTD_MSVC_STATIC_LIB_SUFFIX=%ZSTD_SUFFIX%` to link with
zstd%ZSTD_SUFFIX%.lib.

### Visual Studio

Microsoft provides the free Visual Studio Community edition. When doing
development, you must launch the developer command prompt using:

#### Visual Studio 2015

```
"C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" amd64
```

#### Visual Studio 2017

```
"C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64
```

It's easiest to configure a console emulator like [cmder][3] to automatically
launch this when starting a new development console.

## Building with Ninja and clcache

We recommend the [Ninja](https://ninja-build.org/) build system for better
build parallelization, and the optional
[clcache](https://github.com/frerich/clcache/) compiler cache which keeps
track of past compilations to avoid running them over and over again
(in a way similar to the Unix-specific "ccache").

Activate your conda build environment to first install those utilities:

```shell
activate arrow-dev

conda install -c conda-forge ninja
pip install git+https://github.com/frerich/clcache.git
```

Change working directory in cmd.exe to the root directory of Arrow and
do an out of source build by generating Ninja files:

```shell
cd cpp
mkdir build
cd build
cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
```

## Building with NMake

Activate your conda build environment:

```shell
activate arrow-dev
```

Change working directory in cmd.exe to the root directory of Arrow and
do an out of source build using `nmake`:

```shell
cd cpp
mkdir build
cd build
cmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
nmake
```

When using conda, only release builds are currently supported.

## Building using Visual Studio (MSVC) Solution Files

Activate your conda build environment:

```shell
activate arrow-dev
```

Change working directory in cmd.exe to the root directory of Arrow and
do an out of source build by generating a MSVC solution:

```shell
cd cpp
mkdir build
cd build
cmake -G "Visual Studio 14 2015 Win64" -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
```

## Debug build

To build Debug version of Arrow you should have pre-installed a Debug version
of boost libs.

It's recommended to configure cmake build with the following variables for
Debug build:

`-DARROW_BOOST_USE_SHARED=OFF` - enables static linking with boost debug libs and
simplifies run-time loading of 3rd parties. (Recommended)

`-DBOOST_ROOT` - sets the root directory of boost libs. (Optional)

`-DBOOST_LIBRARYDIR` - sets the directory with boost lib files. (Optional)

Command line to build Arrow in Debug might look as following:

```shell
cd cpp
mkdir build
cd build
cmake -G "Visual Studio 14 2015 Win64" ^
      -DARROW_BOOST_USE_SHARED=OFF ^
      -DCMAKE_BUILD_TYPE=Debug ^
      -DBOOST_ROOT=C:/local/boost_1_63_0  ^
      -DBOOST_LIBRARYDIR=C:/local/boost_1_63_0/lib64-msvc-14.0 ^
      ..
cmake --build . --config Debug
```

To get the latest build instructions, you can reference [cpp-python-msvc-build.bat][5], which is used by automated Appveyor builds.


[1]: https://conda.io/miniconda.html
[2]: https://conda-forge.github.io/
[3]: http://cmder.net/
[4]: https://cmake.org/
[5]: https://github.com/apache/arrow/blob/master/ci/cpp-python-msvc-build.bat
