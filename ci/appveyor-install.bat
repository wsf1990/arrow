@rem Licensed to the Apache Software Foundation (ASF) under one
@rem or more contributor license agreements.  See the NOTICE file
@rem distributed with this work for additional information
@rem regarding copyright ownership.  The ASF licenses this file
@rem to you under the Apache License, Version 2.0 (the
@rem "License"); you may not use this file except in compliance
@rem with the License.  You may obtain a copy of the License at
@rem
@rem   http://www.apache.org/licenses/LICENSE-2.0
@rem
@rem Unless required by applicable law or agreed to in writing,
@rem software distributed under the License is distributed on an
@rem "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
@rem KIND, either express or implied.  See the License for the
@rem specific language governing permissions and limitations
@rem under the License.

@echo on

IF /i "%JOB%" == "C#" goto csharp
IF "%JOB:~,5%" == "MinGW" goto mingw
IF /i "%JOB%" == "rust" goto rust

@rem all else are C++
goto cpp

:cpp
set "PATH=C:\Miniconda36-x64;C:\Miniconda36-x64\Scripts;C:\Miniconda36-x64\Library\bin;%PATH%"
set BOOST_ROOT=C:\Libraries\boost_1_67_0
set BOOST_LIBRARYDIR=C:\Libraries\boost_1_67_0\lib64-msvc-14.0
call ci\appveyor-cpp-setup.bat
goto scriptexit

:rust
curl -sSf -o rustup-init.exe https://win.rustup.rs/
rustup-init.exe -y --default-host %TARGET% --default-toolchain stable
set "PATH=%PATH%;C:\Users\Appveyor\.cargo\bin"
rustup install stable
rustup install nightly
rustc -Vv
cargo -V
goto scriptexit

:mingw
call ci\appveyor-cpp-setup-mingw.bat
goto scriptexit

:csharp

:scriptexit
