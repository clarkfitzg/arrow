#!/bin/bash -ex
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License. See accompanying LICENSE file.

wget https://www.samba.org/ftp/ccache/ccache-3.3.4.tar.bz2 -O ccache-3.3.4.tar.bz2
tar xf ccache-3.3.4.tar.bz2
pushd ccache-3.3.4
./configure --prefix=/usr
make -j5
make install
popd
rm -rf ccache-3.3.4.tar.bz2 ccache-3.3.4
