# Copyright (c) 2026 Axertin
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

include(FetchContent)
FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.5.4 GIT_SHALLOW TRUE SYSTEM)
FetchContent_MakeAvailable(Catch2)

file(GLOB test_sources CONFIGURE_DEPENDS unit/*.cpp)
add_executable(apclient_tests ${test_sources})
target_include_directories(apclient_tests PRIVATE .)
target_link_libraries(apclient_tests PRIVATE apclient_core Catch2::Catch2WithMain dxguid)
add_dependencies(apclient_tests dinput8)
apclient_warnings(apclient_tests)
add_test(NAME unit COMMAND apclient_tests)

# On a Linux host the test exe runs through Wine, with one wineserver in a prefix under the build directory.
if(CMAKE_HOST_UNIX)
    find_program(WINE_EXECUTABLE NAMES wine wine64 REQUIRED)
    find_program(WINESERVER_EXECUTABLE NAMES wineserver wineserver64 REQUIRED
        PATHS /usr/lib/wine /usr/lib/x86_64-linux-gnu/wine /usr/lib64/wine)
    set_target_properties(apclient_tests PROPERTIES CROSSCOMPILING_EMULATOR "${WINE_EXECUTABLE}")

    set(wine_prefix "${CMAKE_BINARY_DIR}/wineprefix")
    file(MAKE_DIRECTORY "${wine_prefix}")
    # dinput8=n,b makes Wine load our dinput8.dll over its builtin. mscoree and mshtml are disabled so
    # prefix creation does not ask to install Mono and Gecko.
    set(wine_env "WINEPREFIX=${wine_prefix}" "WINEDEBUG=-all" "WINEDLLOVERRIDES=dinput8=n,b;mscoree,mshtml=")

    # The fixture stops a server left from an interrupted run, starts one that exits 10 seconds after its
    # last client, and boots the prefix. Wine's daemons keep inherited descriptors open and CTest waits for
    # its output pipe to close, so all output is redirected away from it.
    add_test(NAME wine.start COMMAND sh -c [=[
        "$0" -k 2>/dev/null
        "$0" -p10 </dev/null >/dev/null 2>&1 || exit 1
        exec "$1" wineboot </dev/null >/dev/null 2>&1
    ]=] "${WINESERVER_EXECUTABLE}" "${WINE_EXECUTABLE}")
    set_tests_properties(wine.start PROPERTIES FIXTURES_SETUP wine TIMEOUT 600 ENVIRONMENT "${wine_env}")
    set_tests_properties(unit PROPERTIES FIXTURES_REQUIRED wine ENVIRONMENT "${wine_env}")
endif()
