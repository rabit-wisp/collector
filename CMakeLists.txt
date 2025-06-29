cmake_minimum_required(VERSION 3.20)
project(MyProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXE_LINKER_FLAGS "-static")

# PKG_CONFIG setup for cross-compilation
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_LIBDIR} "${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")
set(CMAKE_PKG_CONFIG_EXECUTABLE "${TOOLCHAIN_PREFIX}/bin/pkg-config")

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
pkg_check_modules(LIBNL REQUIRED libnl-3.0 libnl-genl-3.0)

# Targets
add_executable(wireless-stats src/main.cpp
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
