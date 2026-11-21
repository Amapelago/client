cmake_minimum_required(VERSION 3.25)
project(okami-apclient C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# A std::mutex built with VS 17.10+ headers locks only on msvcp140 14.40 or newer, and Okami's own redistributable installs 14.28. This switch makes the
# constructor initialise through the runtime, which every msvcp140 supports. It is a compile definition because it must reach every translation unit,
# including those that get <mutex> from third-party headers.
add_compile_definitions(_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)

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

# asio, OpenSSL, zlib and nlohmann-json come from vcpkg. The Archipelago net stack (apclientpp on wswrap on websocketpp) is header-only and
# fetched like MinHook: a GitHub archive pinned by SHA256, unpacked without running its CMakeLists.txt.
find_package(asio CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

FetchContent_Declare(websocketpp
    URL https://github.com/zaphoyd/websocketpp/archive/56123c87598f8b1dd471be83ca841ceae07f95ba.tar.gz # 0.8.2
    URL_HASH SHA256=56a040dd99e7e58e0513204030e5af5e94750a3d80b176b16bf8e3a837b40f0a
    DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/build/_downloads"
    SOURCE_SUBDIR no-cmake)
FetchContent_Declare(wswrap
    URL https://github.com/black-sliver/wswrap/archive/aeba7ac428028723fb26ce92488f260660f786b1.tar.gz
    URL_HASH SHA256=3276e3fb143e38189c5bf5eee2b43a34e821c3fa8f7323da965056e73856e2cf
    DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/build/_downloads"
    SOURCE_SUBDIR no-cmake)
FetchContent_Declare(apclientpp
    URL https://github.com/black-sliver/apclientpp/archive/79621690a3e845645f43888b0fe234a99c74892e.tar.gz # 0.6.4
    URL_HASH SHA256=9a5db9a8954dc6d4d541b47948efed22396511846db5a80c2a08e3d7f4da1a57
    DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/build/_downloads"
    SOURCE_SUBDIR no-cmake)
FetchContent_MakeAvailable(websocketpp wswrap apclientpp)

# websocketpp replaces the asio error from a failed TLS handshake with a generic tls_handshake_failed, so a rejected certificate, a version
# mismatch and a reset mid-handshake all report the same error. The rewrite keeps the asio code, which carries OpenSSL's reason. It is
# idempotent, and configure fails if tls.hpp matches neither the original nor the rewritten text.
set(_ws_tls "${websocketpp_SOURCE_DIR}/websocketpp/transport/asio/security/tls.hpp")
set(_ws_tls_old "m_ec = socket::make_error_code(socket::error::tls_handshake_failed);")
set(_ws_tls_new "m_ec = ec;")
file(READ "${_ws_tls}" _ws_tls_text)
string(FIND "${_ws_tls_text}" "${_ws_tls_old}" _ws_tls_at)
if(NOT _ws_tls_at EQUAL -1)
    string(REPLACE "${_ws_tls_old}" "${_ws_tls_new}" _ws_tls_text "${_ws_tls_text}")
    file(WRITE "${_ws_tls}" "${_ws_tls_text}")
elseif(NOT _ws_tls_text MATCHES "if \\(ec\\) \\{\n *m_ec = ec;\n *\\} else \\{")
    message(FATAL_ERROR "websocketpp tls.hpp matches neither the original nor the patched handle_init text")
endif()

# ImGui draws the overlay. It is fetched and built like MinHook, from the core files and the Win32 and DX11 backends.
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.2b.tar.gz
    URL_HASH SHA256=da3d453cce74e0fb3d67f8d798a2a8d04fcaf0b33ce0e0131d0695dfc4f64191
    DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/build/_downloads"
    SOURCE_SUBDIR no-cmake)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC EXCLUDE_FROM_ALL
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp")
target_include_directories(imgui SYSTEM PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
target_link_libraries(imgui PUBLIC dwmapi)

file(GLOB core_sources CONFIGURE_DEPENDS src/support/*.cpp src/game/*.cpp src/ap/*.cpp)
add_library(apclient_core STATIC ${core_sources} src/client.cpp src/boot/fingerprint.cpp)
target_include_directories(apclient_core PUBLIC src)
target_include_directories(apclient_core SYSTEM PUBLIC "${apclientpp_SOURCE_DIR}" "${wswrap_SOURCE_DIR}/include" "${websocketpp_SOURCE_DIR}")
target_link_libraries(apclient_core PUBLIC asio::asio OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB nlohmann_json::nlohmann_json)
add_git_version_info(apclient_core)
apclient_warnings(apclient_core)

file(GLOB ui_sources CONFIGURE_DEPENDS src/ui/*.cpp)
add_library(dinput8 SHARED src/boot/boot.cpp src/boot/proxy.cpp src/boot/dinput8.def ${ui_sources})
set_target_properties(dinput8 PROPERTIES PREFIX "")
target_link_libraries(dinput8 PRIVATE apclient_core minhook imgui)
target_link_options(dinput8 PRIVATE /DEBUG $<$<CONFIG:Release>:/OPT:REF /OPT:ICF>)
apclient_warnings(dinput8)

enable_testing()
add_subdirectory(tests)
