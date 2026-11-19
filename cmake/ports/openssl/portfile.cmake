# OpenSSL for an MSVC-ABI x64 target, built on a Linux host with clang-cl and an xwin sysroot. OpenSSL's Windows makefile needs nmake, so
# Configure takes the VC-WIN64A settings through its unix makefile template (vc-gnumake.conf) and GNU make runs the build.
if(NOT VCPKG_HOST_IS_LINUX OR NOT VCPKG_TARGET_IS_WINDOWS OR VCPKG_TARGET_IS_MINGW OR NOT VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    message(FATAL_ERROR "This openssl port builds only MSVC x64 targets on a Linux host.")
endif()
vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO openssl/openssl
    REF "openssl-${VERSION}"
    SHA512 29002ce50cb95a4f4f1d0e9d3f684401fbd4eac34203dc2eef3b6334af5d44aa46bf788b63a6f5c139c383eafb7269ae87a58a9a3ad5912903b9773e545ccc0a
    PATCHES
        cmake-config.patch
)

vcpkg_find_acquire_program(PERL)
find_program(MAKE make REQUIRED)
find_program(LLVM_AR llvm-ar REQUIRED)
vcpkg_cmake_get_vars(cmake_vars_file)
include("${cmake_vars_file}")

set(build_dir "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel")
file(REMOVE_RECURSE "${build_dir}")
file(MAKE_DIRECTORY "${build_dir}")

# --prefix=/ keeps build paths out of the binaries, and DESTDIR places the install. On Windows the openssldir /etc/ssl is \etc\ssl on the
# current drive, which any user can write to, so no-autoload-config keeps OpenSSL from loading an openssl.cnf from there. The engines and
# modules directories, /lib/engines-3 and /lib/ossl-modules, are read only by an explicit engine or provider load, and the client makes none.
vcpkg_execute_required_process(
    COMMAND "${PERL}" "${SOURCE_PATH}/Configure"
        "--config=${CMAKE_CURRENT_LIST_DIR}/vc-gnumake.conf"
        VC-WIN64A-gnumake
        enable-static-engine
        enable-capieng
        enable-ec_nistp_64_gcc_128
        no-tests
        no-docs
        no-shared
        no-module
        no-apps
        no-autoload-config
        no-makedepend
        --prefix=/
        --libdir=lib
        --openssldir=/etc/ssl
        "CC=${VCPKG_DETECTED_CMAKE_C_COMPILER}"
        "CFLAGS=${VCPKG_COMBINED_C_FLAGS_RELEASE}"
        "AR=${LLVM_AR}"
        "ARFLAGS=r --format=coff"
        "LD=${VCPKG_DETECTED_CMAKE_LINKER}"
    WORKING_DIRECTORY "${build_dir}"
    LOGNAME "configure-${TARGET_TRIPLET}-rel"
)
vcpkg_execute_build_process(
    COMMAND "${MAKE}" "-j${VCPKG_CONCURRENCY}" build_libs
    WORKING_DIRECTORY "${build_dir}"
    LOGNAME "build-${TARGET_TRIPLET}-rel"
)
vcpkg_execute_required_process(
    COMMAND "${MAKE}" install_dev "DESTDIR=${CURRENT_PACKAGES_DIR}"
    WORKING_DIRECTORY "${build_dir}"
    LOGNAME "install-${TARGET_TRIPLET}-rel"
)

foreach(lib IN ITEMS libcrypto libssl)
    file(RENAME "${CURRENT_PACKAGES_DIR}/lib/${lib}.a" "${CURRENT_PACKAGES_DIR}/lib/${lib}.lib")
endforeach()

vcpkg_fixup_pkgconfig()
vcpkg_cmake_config_fixup()

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$" version "${VERSION}")
set(OPENSSL_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(OPENSSL_VERSION_MINOR "${CMAKE_MATCH_2}")
set(OPENSSL_VERSION_FIX "${CMAKE_MATCH_3}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/vcpkg-cmake-wrapper.cmake.in" "${CURRENT_PACKAGES_DIR}/share/${PORT}/vcpkg-cmake-wrapper.cmake" @ONLY)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
