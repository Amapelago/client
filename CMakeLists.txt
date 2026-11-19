cmake_minimum_required(VERSION 3.25)
project(okami-apclient C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# clangd looks for compile_commands.json at the source root, so the most recently configured build links its copy there.
if(CMAKE_EXPORT_COMPILE_COMMANDS)
    file(CREATE_LINK "${CMAKE_BINARY_DIR}/compile_commands.json" "${CMAKE_SOURCE_DIR}/compile_commands.json" SYMBOLIC)
endif()

# /Z7 puts debug info in every configuration. dinput8 also links with /DEBUG so its pdb ships. The warning flags apply to first-party targets only.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)

function(apclient_warnings target)
    target_compile_options(${target} PRIVATE /W4 -Wshadow /WX)
endfunction()

include(cmake/SimpleGitVersion.cmake)

# MinHook is fetched as a release tarball pinned by SHA256. SOURCE_SUBDIR points at a directory that does not exist, so FetchContent_MakeAvailable only
# unpacks and never runs the project's own CMakeLists.txt. The target is built here without the first-party warning flags, and its headers are SYSTEM.
include(FetchContent)
FetchContent_Declare(minhook
    URL https://github.com/TsudaKageyu/minhook/archive/refs/tags/v1.3.4.tar.gz
    URL_HASH SHA256=1aebeae4ca898330c507860acc2fca2eb335fe446a3a2b8444c3bf8b2660a14e
    DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/build/_downloads"
    SOURCE_SUBDIR no-cmake)
FetchContent_MakeAvailable(minhook)

# x64 only, so hde32.c stays out.
add_library(minhook STATIC EXCLUDE_FROM_ALL
    "${minhook_SOURCE_DIR}/src/buffer.c"
    "${minhook_SOURCE_DIR}/src/hook.c"
    "${minhook_SOURCE_DIR}/src/trampoline.c"
    "${minhook_SOURCE_DIR}/src/hde/hde64.c")
target_include_directories(minhook SYSTEM PUBLIC "${minhook_SOURCE_DIR}/include")

file(GLOB core_sources CONFIGURE_DEPENDS src/support/*.cpp src/game/*.cpp)
add_library(apclient_core STATIC ${core_sources} src/client.cpp src/boot/fingerprint.cpp)
target_include_directories(apclient_core PUBLIC src)
add_git_version_info(apclient_core)
apclient_warnings(apclient_core)

add_library(dinput8 SHARED src/boot/boot.cpp src/boot/proxy.cpp src/boot/dinput8.def)
set_target_properties(dinput8 PROPERTIES PREFIX "")
target_link_libraries(dinput8 PRIVATE apclient_core minhook)
target_link_options(dinput8 PRIVATE /DEBUG $<$<CONFIG:Release>:/OPT:REF /OPT:ICF>)
apclient_warnings(dinput8)

enable_testing()
add_subdirectory(tests)
