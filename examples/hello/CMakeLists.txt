# hello: a BRITE application, and the pattern for your own.
#
# This file is two things at once. Built from BRITE's own tree (cmake -S . -B build, the default when BRITE is the
# top-level project) it just adds the `hello` target. Used as a project of its own -- copy this folder anywhere, set
# BRITE_DIR to where BRITE-Engine is -- it adds BRITE as a subdirectory first, which is how your project uses BRITE.
cmake_minimum_required(VERSION 3.20)
project(Hello CXX)

if(NOT TARGET BRITE::Raylib)
    # Where BRITE-Engine is: a clone beside your project, a git submodule, wherever. The default is this tree.
    set(BRITE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "The BRITE-Engine folder")

    # BRITE builds everything by default and downloads what it builds. A window needs only the raylib module:
    # switch off the rest, which also skips their downloads.
    set(BRITE_BUILD_BOX2D OFF)
    set(BRITE_BUILD_BOX3D OFF)
    set(BRITE_BUILD_AUDIO OFF)
    set(BRITE_BUILD_PERSISTENCE OFF)
    set(BRITE_BUILD_CONTENT OFF)
    set(BRITE_BUILD_DEBUG_UI OFF)

    add_subdirectory("${BRITE_DIR}" brite)
endif()

add_executable(hello main.cpp)
target_link_libraries(hello PRIVATE BRITE::Raylib)
