cmake_minimum_required(VERSION 3.20)
project(iw-telemetry LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/binaries/)

include(FetchContent)

# Fetch docopt
FetchContent_Declare(
  docopt
  GIT_REPOSITORY https://github.com/docopt/docopt.cpp.git
  GIT_SHALLOW TRUE
  GIT_TAG v0.6.3
)
FetchContent_MakeAvailable(docopt)

set(ZLIB_USE_STATIC_LIBS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZLIB REQUIRED zlib)
pkg_check_modules(ZMQ REQUIRED libzmq)

set(LIBNL_INCLUDE_DIRS "${CMAKE_SYSROOT}/usr/include/libnl3")
set(LIBNL_LIBRARIES "-lnl-3 -lnl-genl-3")

add_executable(iw-telemetry src/wireless-stats.cpp
                              src/output.cpp
                              src/station.cpp
                              src/ping.cpp
                              src/ppp.cpp
                              src/survey.cpp)

target_include_directories(iw-telemetry PRIVATE
                                          ${DOCOPT_INCLUDE_DIRS}
                                          ${LIBNL_INCLUDE_DIRS})

target_link_libraries(iw-telemetry PRIVATE
                                     nl-3
                                     nl-genl-3
                                     ${ZMQ_LIBRARIES}
                                     ${ZLIB_LIBRARIES}
                                     ${LIBNL_LIBRARIES}
                                     docopt_s)
