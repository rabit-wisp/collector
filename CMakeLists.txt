cmake_minimum_required(VERSION 3.20)
project(telemetry LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# PKG_CONFIG setup for cross-compilation
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_LIBDIR} "${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")
set(CMAKE_PKG_CONFIG_EXECUTABLE "${TOOLCHAIN_PREFIX}/bin/pkg-config")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/binaries/)

# Apply properties to all executables at once
function(apply_target_to_executable_names)
  get_directory_property(targets BUILDSYSTEM_TARGETS)
  foreach(target ${targets})
      get_target_property(target_type ${target} TYPE)
      if(target_type STREQUAL "EXECUTABLE")
          # Do something with each executable
          set_target_properties(${target} PROPERTIES OUTPUT_NAME "${target}.${CMAKE_SYSTEM_PROCESSOR}")
      endif()

      # Only strip in Release builds
      if(CMAKE_BUILD_TYPE STREQUAL "Release")
          add_custom_command(TARGET ${target} POST_BUILD
          COMMAND ${CMAKE_STRIP} $<TARGET_FILE:${target}> COMMENT "Stripping ${target}")
      endif()
  endforeach()
endfunction()

include(FetchContent)


# Fetch docopt
FetchContent_Declare(
  docopt
  GIT_REPOSITORY https://github.com/docopt/docopt.cpp.git
  GIT_SHALLOW TRUE
  GIT_TAG v0.6.3
)
FetchContent_MakeAvailable(docopt)

# Fetch fmt
FetchContent_Declare(
fmt
GIT_REPOSITORY https://github.com/fmtlib/fmt.git
GIT_SHALLOW TRUE
GIT_TAG 11.2.0
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZLIB REQUIRED zlib)
pkg_check_modules(LIBNL REQUIRED libnl-3.0)

# Targets
add_executable(wireless-stats src/wireless-stats.cpp
                              src/output.cpp
                              src/station.cpp
                              src/ppp.cpp
                              src/survey.cpp)

target_include_directories(wireless-stats PRIVATE
                                          ${DOCOPT_INCLUDE_DIRS}
                                          ${LIBNL_INCLUDE_DIRS})

target_link_libraries(wireless-stats PRIVATE
                                             nl-3
                                             nl-genl-3
                                             zmq
                                             ${ZLIB_LIBRARIES}
                                             ${LIBNL_LIBRARIES}
                                             #simdjson::simdjson
                                             docopt_s
)

apply_target_to_executable_names()
