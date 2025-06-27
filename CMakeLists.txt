cmake_minimum_required(VERSION 3.20)
project(MyProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXE_LINKER_FLAGS "-static")
add_compile_options(-msoft-float)

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

FetchContent_Declare(
  simdjson
  GIT_REPOSITORY https://github.com/simdjson/simdjson.git
  GIT_SHALLOW TRUE
  GIT_TAG tags/v1.0.0
  )

FetchContent_MakeAvailable(simdjson)

# # Fetch and build zlib as static library only
# set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
# set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "Build zlib examples")
# # Fetch and build zlib
# FetchContent_Declare(
# zlib
# GIT_REPOSITORY https://github.com/madler/zlib.git
# GIT_SHALLOW TRUE
# GIT_TAG v1.3.1
# )
# FetchContent_MakeAvailable(zlib)

# Build libnl from source using ExternalProject
# libnl uses autotools, not CMake, so we need to use ExternalProject
# include(ExternalProject)

# # set(LIBNL_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/libnl-build)
# # set(LIBNL_INSTALL_DIR ${LIBNL_PREFIX})
# # set(LIBNL_INCLUDE_DIRS ${LIBNL_INSTALL_DIR}/include/libnl3)

# # Create the directories that will be used by the imported targets
# file(MAKE_DIRECTORY ${LIBNL_INSTALL_DIR}/include)
# file(MAKE_DIRECTORY ${LIBNL_INSTALL_DIR}/lib)

# link_directories(${LIBNL_INSTALL_DIR}/lib)

# ExternalProject_Add(
# libnl_external
# GIT_REPOSITORY https://github.com/thom311/libnl.git
# GIT_TAG libnl3_10_0
# GIT_SHALLOW TRUE
# PREFIX ${LIBNL_PREFIX}
# CONFIGURE_COMMAND <SOURCE_DIR>/autogen.sh COMMAND
# <SOURCE_DIR>/configure
# --prefix=<INSTALL_DIR>
# --enable-static
# --disable-shared
# --disable-cli
# --disable-pthreads
# --disable-debug
# --host=${CMAKE_SYSTEM_PROCESSOR}-linux-gnu
# CC=${CMAKE_C_COMPILER}
# AR=${CMAKE_AR}
# RANLIB=${CMAKE_RANLIB}
# "CFLAGS=${CMAKE_C_FLAGS} -msoft-float"
# BUILD_COMMAND make -j${CMAKE_BUILD_PARALLEL_LEVEL}
# INSTALL_COMMAND make install
# BUILD_BYPRODUCTS
# ${LIBNL_INSTALL_DIR}/lib/libnl-3.a
# ${LIBNL_INSTALL_DIR}/lib/libnl-genl-3.a)

# add_library(libnl-3 STATIC IMPORTED GLOBAL)
# set_target_properties(libnl-3 PROPERTIES
#                               IMPORTED_LOCATION ${LIBNL_INSTALL_DIR}/lib/libnl-3.a
#                               INTERFACE_INCLUDE_DIRECTORIES ${LIBNL_INSTALL_DIR}/include)

# add_library(libnl-genl-3 STATIC IMPORTED GLOBAL)
# set_target_properties(libnl-genl-3 PROPERTIES
#                                    IMPORTED_LOCATION ${LIBNL_INSTALL_DIR}/lib/libnl-genl-3.a
#                                    INTERFACE_INCLUDE_DIRECTORIES ${LIBNL_INSTALL_DIR}/include)

# Make sure the imported targets depend on the external project
#add_dependencies(libnl libnl_external)
#add_dependencies(libnl-genl-3 libnl_external)

set(LIBNL_INCLUDE_DIRS "${CMAKE_SYSROOT}/usr/include/libnl3")
set(LIBNL_LIBRARIES "-lnl-3 -lnl-genl-3")

# Verify the paths exist
if(NOT EXISTS "${LIBNL_INCLUDE_DIRS}")
message(FATAL_ERROR "libnl headers not found at ${LIBNL_INCLUDE_DIRS}")
endif()

# Targets
add_executable(wireless-stats #src/test.cpp
                              src/main.cpp
                              src/station.cpp
                              src/compress.cpp
                              src/ppp.cpp
                              src/survey.cpp)

target_include_directories(wireless-stats PRIVATE ${LIBNL_INCLUDE_DIRS})
target_link_libraries(wireless-stats PRIVATE ${LIBNL_LIBRARIES})

# add_dependencies(wireless-stats libnl_external)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZLIB REQUIRED zlib)
#pkg_check_modules(DOCOPT REQUIRED docopt)
pkg_check_modules(LIBNL REQUIRED libnl-3.0 libnl-genl-3.0)

target_include_directories(wireless-stats PRIVATE ${ZLIB_INCLUDE_DIRS} ${LIBNL_INCLUDE_DIRS})
target_link_libraries(wireless-stats PRIVATE
                                             nl-3
                                             nl-genl-3
                                             #libnl-genl-3
                                             #libnl::nl-3
                                             #libnl::nl-genl-3
                                             ${ZLIB_LIBRARIES}
                                             ${LIBNL_LIBRARIES}
                                             #${DOCOPT_LIBRARIES}
                                             #${ZeroMQ_LIBRARIES}
                                             simdjson::simdjson
                                             docopt_s
)
