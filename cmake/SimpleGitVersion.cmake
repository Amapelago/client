# SimpleGitVersion.cmake - GitHub Flow friendly versioning

# Writes ${CMAKE_BINARY_DIR}/generated/version.hpp, with version::string and version::hash, and adds that folder to target's include path.
function(add_git_version_info target)
  find_package(Git QUIET)

  # Re-run configure (regenerating version.hpp) when HEAD or the branch ref moves: a commit rewrites .git/refs/heads/<branch>, a switch rewrites
  # .git/HEAD, and packed-refs is the fallback.
  set(_git_head "${CMAKE_SOURCE_DIR}/.git/HEAD")
  if(EXISTS "${_git_head}")
    set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_git_head}")
    file(STRINGS "${_git_head}" _git_head_line LIMIT_COUNT 1)
    if(_git_head_line MATCHES "^ref: (.+)$")
      set(_git_ref "${CMAKE_SOURCE_DIR}/.git/${CMAKE_MATCH_1}")
      if(EXISTS "${_git_ref}")
        set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_git_ref}")
      endif()
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/.git/packed-refs")
      set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/.git/packed-refs")
    endif()
  endif()

  if(NOT GIT_FOUND)
    set(VERSION_STRING "0.0.0-unknown")
    set(VERSION_HASH "unknown")
  else()
    execute_process(
      COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      OUTPUT_VARIABLE GIT_DESCRIBE
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    execute_process(
      COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      OUTPUT_VARIABLE GIT_HASH
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )

    if(GIT_DESCRIBE MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)(-([a-zA-Z][a-zA-Z0-9.-]*))?")
      set(VERSION_MAJOR ${CMAKE_MATCH_1})
      set(VERSION_MINOR ${CMAKE_MATCH_2})
      set(VERSION_PATCH ${CMAKE_MATCH_3})
      set(VERSION_PRERELEASE ${CMAKE_MATCH_5})

      set(BASE_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
      if(VERSION_PRERELEASE)
        set(BASE_VERSION "${BASE_VERSION}-${VERSION_PRERELEASE}")
      endif()

      if(GIT_DESCRIBE MATCHES "-([0-9]+)-g[a-f0-9]+")
        # Ahead of the latest tag by N commits: bump the patch and drop any prerelease suffix, so the -dev prerelease sorts after the tag (including
        # -beta/-rc tags).
        set(COMMITS_SINCE_TAG ${CMAKE_MATCH_1})
        math(EXPR VERSION_PATCH "${VERSION_PATCH} + 1")
        set(VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}-dev.${COMMITS_SINCE_TAG}")
        if(GIT_DESCRIBE MATCHES "dirty$")
          set(VERSION_STRING "${VERSION_STRING}-dirty")
        endif()
      else()
        # Exactly on the tag (any -dirty or prerelease suffix is already in BASE_VERSION).
        set(VERSION_STRING "${BASE_VERSION}")
      endif()
    else()
      set(VERSION_STRING "0.0.0-dev")
    endif()

    set(VERSION_HASH ${GIT_HASH})
  endif()

  set(VERSION_HEADER_DIR "${CMAKE_BINARY_DIR}/generated")
  set(VERSION_HEADER "${VERSION_HEADER_DIR}/version.hpp")

  file(MAKE_DIRECTORY ${VERSION_HEADER_DIR})
  # Staged through a temp + configure_file: .git/HEAD is a configure dependency, so an unconditional write would bump the header's mtime and relink
  # on every commit.
  file(WRITE "${VERSION_HEADER}.in"
    "#pragma once
#include <string_view>
namespace version {
    inline constexpr std::string_view string{\"${VERSION_STRING}\"};
    inline constexpr std::string_view hash{\"${VERSION_HASH}\"};
}
")
  configure_file("${VERSION_HEADER}.in" "${VERSION_HEADER}" COPYONLY)

  target_include_directories(${target} PUBLIC ${VERSION_HEADER_DIR})
  message(STATUS "okami-apclient version: ${VERSION_STRING} (${VERSION_HASH})")
endfunction()
