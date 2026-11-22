# Copyright (c) 2026 Axertin
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# x64 MSVC-ABI static libraries with the dynamic release CRT, built on Linux with clang-cl + xwin.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)

# Empty system name is what vcpkg means by MSVC-ABI Windows; "Windows" leaves VCPKG_TARGET_IS_WINDOWS off.
set(VCPKG_CMAKE_SYSTEM_NAME "")
set(VCPKG_ENV_PASSTHROUGH XWIN_SDK)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../clang-cl-xwin-x64.cmake")
