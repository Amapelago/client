# Copyright (c) 2026 Axertin
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Stages each linked dependency's own licence file beside dinput8.dll.
#
# One level deep: these are the libraries the DLL links, and what they vendor is reachable from their upstream. Sources are the trees the build already
# fetched.

# Fetched but not linked into the DLL. Catch2 builds the tests only.
set(_NOTICE_SKIP catch2)

# Stage LICENSE into <dir> and each component's licence files under <dir>/third_party_notices/<component>/, and return the staged paths in out_var. They
# are custom-command outputs, so a build restages any that are deleted.
function(stage_notices dir out_var)
    set(_dir "${dir}/third_party_notices")
    set(_staged "")
    set(_missing "")

    # FetchContent checkouts: <name>-src/{LICENSE*,COPYING*,LICENSES/*}.
    file(GLOB _dep_dirs "${FETCHCONTENT_BASE_DIR}/*-src")
    foreach(_dep_dir IN LISTS _dep_dirs)
        get_filename_component(_dep "${_dep_dir}" NAME)
        string(REGEX REPLACE "-src$" "" _dep "${_dep}")
        if(_dep IN_LIST _NOTICE_SKIP)
            continue()
        endif()

        file(GLOB _files LIST_DIRECTORIES false "${_dep_dir}/LICENSE*" "${_dep_dir}/COPYING*" "${_dep_dir}/LICENSES/*")
        if(NOT _files)
            list(APPEND _missing "${_dep}")
            continue()
        endif()
        _stage_notice_files("${_dir}" "${_dep}" "${_files}" _staged)
    endforeach()

    # vcpkg ports: the copyright file vcpkg installs for every port it builds.
    file(GLOB _copyrights "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/*/copyright")
    foreach(_copyright IN LISTS _copyrights)
        get_filename_component(_port "${_copyright}" DIRECTORY)
        get_filename_component(_port "${_port}" NAME)
        _stage_notice_files("${_dir}" "${_port}" "${_copyright}" _staged)
    endforeach()

    if(_missing)
        list(JOIN _missing ", " _missing_text)
        message(WARNING "notices: no licence file found for ${_missing_text}")
    endif()

    # Configured into the build tree, then copied in as an output.
    set(_readme_src "${CMAKE_BINARY_DIR}/generated/third_party_notices.md")
    configure_file("${CMAKE_SOURCE_DIR}/packaging/third_party_notices.md.in" "${_readme_src}" @ONLY)
    add_custom_command(
        OUTPUT "${_dir}/README.md"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_readme_src}" "${_dir}/README.md"
        DEPENDS "${_readme_src}"
        VERBATIM
        COMMENT "Staging third_party_notices/README.md")
    list(APPEND _staged "${_dir}/README.md")

    add_custom_command(
        OUTPUT "${dir}/LICENSE"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_SOURCE_DIR}/LICENSE" "${dir}/LICENSE"
        DEPENDS "${CMAKE_SOURCE_DIR}/LICENSE"
        VERBATIM
        COMMENT "Staging LICENSE")
    list(APPEND _staged "${dir}/LICENSE")

    set(${out_var} "${_staged}" PARENT_SCOPE)
endfunction()

# Copy one component's licence files under their upstream names.
function(_stage_notice_files dir component files staged_var)
    set(_staged "${${staged_var}}")
    foreach(_file IN LISTS files)
        get_filename_component(_name "${_file}" NAME)
        set(_out "${dir}/${component}/${_name}")
        add_custom_command(
            OUTPUT "${_out}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_file}" "${_out}"
            DEPENDS "${_file}"
            VERBATIM
            COMMENT "Staging third_party_notices/${component}/${_name}")
        list(APPEND _staged "${_out}")
    endforeach()
    set(${staged_var} "${_staged}" PARENT_SCOPE)
endfunction()
