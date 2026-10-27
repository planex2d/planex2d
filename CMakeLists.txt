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

# AUTO: the OCC bindings when the building Python has netgen (a development build, pip install --no-build-isolation),
# else the core alone (pip's isolated build of the sdist; the OCC bindings then come from planex2d-occ)
set(PLANEX_OCC AUTO CACHE STRING "the OCC bindings _planex_occ (needs netgen and OpenCASCADE): ON, OFF or AUTO")
option(PLANEX_CORE "the core module _planex; off builds only the OCC bindings, as planex2d-occ does from this sdist" ON)
set(PLANEX_OCC_DESTINATION planex CACHE STRING "where the OCC bindings are installed (planex2d-occ: one folder per netgen version)")
# the planex2d version of these sources (planex2d-occ gives it): the OCC bindings refuse a core of another one
if(NOT DEFINED PLANEX_VERSION)
    set(PLANEX_VERSION "${SKBUILD_PROJECT_VERSION}")
endif()
set(PLANEX_CORE_PYBIND11_INTERNALS "" CACHE STRING
    "tests: the core's pybind11 registry version (13: another registry than netgen's, as when netgen moves to another pybind11)")
option(PLANEX_CONSTRAINTS "px.Sketch: the constraint sketch and its solver (needs Eigen); off leaves px.QuickSketch" ON)

if(PLANEX_OCC STREQUAL "AUTO")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(COMMAND ${Python3_EXECUTABLE} -c "import netgen.config" RESULT_VARIABLE planex_netgen_missing
                    OUTPUT_QUIET ERROR_QUIET)
    if(planex_netgen_missing EQUAL 0)
        set(PLANEX_WITH_OCC ON)
    else()
        set(PLANEX_WITH_OCC OFF)
    endif()
else()
    set(PLANEX_WITH_OCC ${PLANEX_OCC})
endif()
message(STATUS "planex: OCC bindings ${PLANEX_WITH_OCC} (PLANEX_OCC=${PLANEX_OCC})")

if(PLANEX_WITH_OCC)
    # without Netgen_DIR: netgen and OpenCASCADE from the pip wheels of the Python that builds (netgen says where its
    # CMake files are; the OCC headers of netgen-occt-devel in its prefix), so that no other OpenCASCADE is picked
    if(NOT Netgen_DIR)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        execute_process(COMMAND ${Python3_EXECUTABLE} -c "import netgen.config; print(netgen.config.get_cmake_dir())"
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
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)
find_package(Threads REQUIRED)
if(PLANEX_CORE)
    # the version the core is tested with: pybind11's overload resolution changed between 3.0 and 3.1
    find_package(pybind11 3.1 CONFIG REQUIRED)
endif()

# a binding file takes up to 1.4 GB to compile (measured 2026-10-10); 3 GB per compile leaves room for everything else
# running, more parallel compiles than the memory holds swap and take many times longer
cmake_host_system_information(RESULT PLANEX_MEMORY_MB QUERY TOTAL_PHYSICAL_MEMORY)
math(EXPR PLANEX_COMPILE_JOBS "${PLANEX_MEMORY_MB} / 3072")
if(PLANEX_COMPILE_JOBS LESS 1)
    set(PLANEX_COMPILE_JOBS 1)
endif()
set_property(GLOBAL APPEND PROPERTY JOB_POOLS planex_compile=${PLANEX_COMPILE_JOBS})

# kind core: pybind11 from pip (any 3.x); kind occ: netgen's pybind11 and the flags of its ngcore, so that the OCC
# bindings share netgen's type registry. When the core's registry is another one, the core's objects reach the OCC
# bindings through pybind11's conduit (binding.hpp: IsA, Shadow, ToCore); only the OCC bindings link netgen and OCC
function(planex_module name kind)
    Python3_add_library(${name} MODULE WITH_SOABI ${ARGN})
    # pybind11's rule for extension modules: no symbol of one binds in another (with GCC the statics of inline
    # functions, pybind11's own among them, are otherwise one per process); the core and the OCC bindings can be built
    # with different pybind11
    set_target_properties(${name} PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    target_include_directories(${name} PRIVATE src external_dependencies/taskflow)
    if(PLANEX_CONSTRAINTS)
        # Eigen 3.4.0, the submodule only (THIRD_PARTY_NOTICES.md): a system include, so its headers are exempt from our
        # warnings (shadowing is an error with GCC); EIGEN_MPL2_ONLY makes including any of its LGPL parts a compile error
        target_include_directories(${name} SYSTEM PRIVATE external_dependencies/eigen)
        target_compile_definitions(${name} PRIVATE PLANEX_CONSTRAINTS EIGEN_MPL2_ONLY)
    endif()
    if(kind STREQUAL "occ")
        target_include_directories(${name} PRIVATE $<TARGET_PROPERTY:ngcore,INTERFACE_INCLUDE_DIRECTORIES>)
        target_compile_definitions(${name} PRIVATE $<TARGET_PROPERTY:ngcore,INTERFACE_COMPILE_DEFINITIONS> PLANEX_OCC_MODULE)
        target_compile_options(${name} PRIVATE $<TARGET_PROPERTY:ngcore,INTERFACE_COMPILE_OPTIONS>)
    else()
        target_link_libraries(${name} PRIVATE pybind11::headers)
        if(PLANEX_CORE_PYBIND11_INTERNALS)
            target_compile_definitions(${name} PRIVATE PYBIND11_INTERNALS_VERSION=${PLANEX_CORE_PYBIND11_INTERNALS})
        endif()
        # the CPU flags netgen gives the OCC bindings, so that the core computes the same numbers
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64" AND NOT MSVC)
            target_compile_options(${name} PRIVATE -march=core-avx2)
        endif()
    endif()
    target_compile_definitions(${name} PRIVATE PLANEX_VERSION="${PLANEX_VERSION}")
    target_link_libraries(${name} PRIVATE Threads::Threads)
    set_property(TARGET ${name} PROPERTY JOB_POOL_COMPILE planex_compile)
    # netgen's flags allow FMA; GCC fuses a*b + c across statements by default, clang only within one expression, and
    # the results differ in the last bits. GCC before 14 reads "on" as "off"
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${name} PRIVATE -ffp-contract=on)
    endif()
    if(kind STREQUAL "occ")
        install(TARGETS ${name} LIBRARY DESTINATION ${PLANEX_OCC_DESTINATION})
    else()
        install(TARGETS ${name} LIBRARY DESTINATION planex)
    endif()
endfunction()

if(PLANEX_CORE)
    planex_module(_planex core
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
        src/python/bindings/binding_sketch.cpp
        src/python/bindings/binding_identify.cpp
        src/python/bindings/binding_step.cpp)
    if(PLANEX_CONSTRAINTS)
        target_sources(_planex PRIVATE src/python/bindings/binding_solver.cpp src/python/bindings/binding_constraintsketch.cpp)
    endif()
endif()

if(PLANEX_WITH_OCC)
    planex_module(_planex_occ occ
        src/python/bindings/module_occ.cpp
        src/python/bindings/binding_occ.cpp
        src/python/bindings/binding_sketch_occ.cpp
        src/python/bindings/binding_section.cpp)
    target_include_directories(_planex_occ PRIVATE ${OpenCASCADE_INCLUDE_DIR})
    target_link_libraries(_planex_occ PRIVATE nglib ngcore ${NETGEN_OCC_LIBRARIES})
    target_compile_definitions(_planex_occ PRIVATE OCCGEOMETRY)
    set_target_properties(_planex_occ PROPERTIES BUILD_RPATH "${NETGEN_LIBRARY_DIR};${OpenCASCADE_LIBRARY_DIR}")
    if(PLANEX_CORE)
        # a development build: the libraries where this machine has them
        set_target_properties(_planex_occ PROPERTIES INSTALL_RPATH "${NETGEN_LIBRARY_DIR};${OpenCASCADE_LIBRARY_DIR}")
    else()
        # planex2d-occ: the module at PLANEX_OCC_DESTINATION in site-packages finds netgen's and OpenCASCADE's libraries
        # where their wheels put them (site-packages/netgen_mesher.libs or netgen, and <prefix>/lib two levels above
        # site-packages); Windows finds the DLLs in the folders netgen registers when it is imported
        string(REPLACE "/" ";" planex_occ_levels "${PLANEX_OCC_DESTINATION}")
        set(planex_occ_up "")
        foreach(level IN LISTS planex_occ_levels)
            string(APPEND planex_occ_up "/..")
        endforeach()
        if(APPLE)
            set_target_properties(_planex_occ PROPERTIES
                INSTALL_RPATH "@loader_path${planex_occ_up}/netgen;@loader_path${planex_occ_up}/../..")
        elseif(UNIX)
            set_target_properties(_planex_occ PROPERTIES
                INSTALL_RPATH "$ORIGIN${planex_occ_up}/netgen_mesher.libs;$ORIGIN${planex_occ_up}/../..")
        endif()
    endif()
endif()
