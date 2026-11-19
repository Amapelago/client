# Cross toolchain for x64 MSVC-ABI Windows binaries on Linux: clang-cl with an xwin-splatted CRT
# and Windows SDK. The sysroot comes from the XWIN_SDK env var or -DXWIN_SDK=<dir>.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# try_compile probes link a real executable, so library checks such as FindThreads' pthread lookup give true answers. They build in
# Release because projects on the old MSVC runtime policy put /MDd in their Debug flags, and the sysroot has no debug CRT import libraries.
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)

if(NOT DEFINED XWIN_SDK)
    if(DEFINED ENV{XWIN_SDK})
        set(XWIN_SDK "$ENV{XWIN_SDK}")
    else()
        set(XWIN_SDK "/opt/xwin-sdk")
    endif()
endif()

set(CMAKE_C_COMPILER /usr/bin/clang-cl)
set(CMAKE_CXX_COMPILER /usr/bin/clang-cl)
set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_LINKER /opt/llvm-mingw/bin/lld-link)
set(CMAKE_AR /usr/bin/llvm-lib)
set(CMAKE_RC_COMPILER /usr/bin/llvm-rc)
set(CMAKE_MT /usr/bin/llvm-mt)

# /imsvc marks the sysroot headers as system headers, so warnings and /WX ignore them.
set(_xwin_inc "/imsvc ${XWIN_SDK}/crt/include /imsvc ${XWIN_SDK}/sdk/include/ucrt /imsvc ${XWIN_SDK}/sdk/include/um /imsvc ${XWIN_SDK}/sdk/include/shared")

# Release dynamic CRT (/MD) in every configuration: players and Wine have only the release
# runtime DLLs, and the sysroot holds only the release import libs. CMake targets take it from
# CMAKE_MSVC_RUNTIME_LIBRARY; /MD is also in the init flags so projects not built with CMake
# get the same CRT.
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
string(APPEND _xwin_inc " /MD")
set(CMAKE_C_FLAGS_INIT "${_xwin_inc}")
set(CMAKE_CXX_FLAGS_INIT "${_xwin_inc}")

set(_xwin_link "/libpath:${XWIN_SDK}/crt/lib/x86_64 /libpath:${XWIN_SDK}/sdk/lib/ucrt/x86_64 /libpath:${XWIN_SDK}/sdk/lib/um/x86_64")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_xwin_link}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_xwin_link}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_xwin_link}")

# Programs come from the Linux host. Libraries, headers and packages are searched in both the
# sysroot and the normal prefixes.
set(CMAKE_FIND_ROOT_PATH "${XWIN_SDK}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
