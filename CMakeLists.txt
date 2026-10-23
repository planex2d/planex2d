# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

cmake_minimum_required(VERSION 3.20)
project(planex LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

# without Netgen_DIR: netgen and OpenCASCADE from the pip wheels of the Python that builds (its netgen package, the
# OCC headers of netgen-occt-devel in its prefix), so that no other OpenCASCADE on the system is picked
if(NOT Netgen_DIR)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} -c "import importlib.util, os; print(os.path.join(importlib.util.find_spec('netgen').submodule_search_locations[0], 'cmake'))"
        OUTPUT_VARIABLE Netgen_DIR OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND ${Python3_EXECUTABLE} -c "import sys; print(sys.prefix)"
        OUTPUT_VARIABLE PYTHON_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    list(PREPEND CMAKE_PREFIX_PATH ${PYTHON_PREFIX})
endif()
find_package(Netgen REQUIRED CONFIG)
# netgen's OCC paths are those of its build machine (pip wheels): OCC comes from its own config, netgen's OCC
# library names resolve to its targets
find_package(OpenCASCADE REQUIRED CONFIG)
if(NOT SKBUILD AND NOT Python3_EXECUTABLE)
    set(Python3_EXECUTABLE ${NETGEN_PYTHON_EXECUTABLE})
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)
find_package(Threads REQUIRED)
option(PLANEX_CONSTRAINTS "px.Sketch: the constraint sketch and its solver (needs Eigen); off leaves px.QuickSketch" ON)

Python3_add_library(_planex MODULE WITH_SOABI
    src/python/bindings/module.cpp
    src/python/bindings/binding_config.cpp
    src/python/bindings/binding_vertex.cpp
    src/python/bindings/binding_edgegeometry.cpp
    src/python/bindings/binding_edges.cpp
    src/python/bindings/binding_wire.cpp
    src/python/bindings/binding_face.cpp
    src/python/bindings/binding_shape.cpp
    src/python/bindings/binding_deformable.cpp
    src/python/bindings/binding_plane.cpp
    src/python/bindings/binding_occ.cpp
    src/python/bindings/binding_sketch.cpp
    src/python/bindings/binding_section.cpp
    src/python/bindings/binding_identify.cpp)
target_include_directories(_planex PRIVATE src ${OpenCASCADE_INCLUDE_DIR} external_dependencies/taskflow)
if(PLANEX_CONSTRAINTS)
    target_sources(_planex PRIVATE src/python/bindings/binding_solver.cpp src/python/bindings/binding_constraintsketch.cpp)
    # Eigen 3.4.0, the submodule only (THIRD_PARTY_NOTICES.md): a system include, so its headers are exempt from our
    # warnings (shadowing is an error with GCC); EIGEN_MPL2_ONLY makes including any of its LGPL parts a compile error
    target_include_directories(_planex SYSTEM PRIVATE external_dependencies/eigen)
    target_compile_definitions(_planex PRIVATE PLANEX_CONSTRAINTS EIGEN_MPL2_ONLY)
endif()
# a binding file takes up to 1.4 GB to compile (measured 2026-10-10); 3 GB per compile leaves room for everything else
# running, more parallel compiles than the memory holds swap and take many times longer
cmake_host_system_information(RESULT PLANEX_MEMORY_MB QUERY TOTAL_PHYSICAL_MEMORY)
math(EXPR PLANEX_COMPILE_JOBS "${PLANEX_MEMORY_MB} / 3072")
if(PLANEX_COMPILE_JOBS LESS 1)
    set(PLANEX_COMPILE_JOBS 1)
endif()
set_property(GLOBAL APPEND PROPERTY JOB_POOLS planex_compile=${PLANEX_COMPILE_JOBS})
set_property(TARGET _planex PROPERTY JOB_POOL_COMPILE planex_compile)
target_link_libraries(_planex PRIVATE nglib ngcore ${NETGEN_OCC_LIBRARIES} Threads::Threads)
target_compile_definitions(_planex PRIVATE OCCGEOMETRY)
# netgen's flags allow FMA; GCC fuses a*b + c across statements by default, clang only within one expression, and the
# results differ in the last bits. GCC before 14 reads "on" as "off"
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(_planex PRIVATE -ffp-contract=on)
endif()
set_target_properties(_planex PROPERTIES
    BUILD_RPATH "${NETGEN_LIBRARY_DIR};${OpenCASCADE_LIBRARY_DIR}"
    INSTALL_RPATH "${NETGEN_LIBRARY_DIR};${OpenCASCADE_LIBRARY_DIR}")

install(TARGETS _planex LIBRARY DESTINATION planex)
