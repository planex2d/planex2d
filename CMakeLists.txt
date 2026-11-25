# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

cmake_minimum_required(VERSION 3.20)
project(planex LANGUAGES CXX)
include(GNUInstallDirs)

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
# the planex2d version of these sources (planex2d-occ gives it): the OCC bindings refuse a core of another one; the
# version as written (0.2.0.dev1), SKBUILD_PROJECT_VERSION has only its release numbers (0.2.0)
if(NOT DEFINED PLANEX_VERSION)
    set(PLANEX_VERSION "${SKBUILD_PROJECT_VERSION_FULL}")
endif()
set(PLANEX_CORE_PYBIND11_INTERNALS "" CACHE STRING
    "tests: the core's pybind11 registry version (13: another registry than netgen's, as when netgen moves to another pybind11)")
# Emscripten without threads (Pyodide) cannot start a thread pool's workers
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten" AND NOT CMAKE_CXX_FLAGS MATCHES "-pthread")
    set(planex_threads_default OFF)
else()
    set(planex_threads_default ON)
endif()
option(PLANEX_THREADS "multithreaded=True on a thread pool (needs Taskflow); off runs every call in the calling thread"
       ${planex_threads_default})
option(PLANEX_CONSTRAINTS "px.Sketch: the constraint sketch and its solver (needs Eigen); off leaves px.QuickSketch" ON)
# the Python package's build (scikit-build) and planex2d-occ's (PLANEX_CORE=OFF, CMake run directly) make the modules;
# a C++ project that adds or installs planex builds the libraries alone, without Python or pybind11
if(SKBUILD OR NOT PLANEX_CORE)
    set(planex_python_default ON)
else()
    set(planex_python_default OFF)
endif()
option(PLANEX_PYTHON "the Python modules _planex and _planex_occ" ${planex_python_default})

if(PLANEX_OCC STREQUAL "AUTO" AND NOT PLANEX_PYTHON)
    set(PLANEX_WITH_OCC OFF)
elseif(PLANEX_OCC STREQUAL "AUTO")
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
if(PLANEX_PYTHON AND CMAKE_SYSTEM_NAME STREQUAL "Emscripten" AND NOT PLANEX_CORE)
    # planex2d-occ's Pyodide build: only the target Python's headers, no interpreter of it to run
    find_package(Python3 REQUIRED COMPONENTS Development.Module)
elseif(PLANEX_PYTHON)
    find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)
endif()
find_package(Threads REQUIRED)
if(PLANEX_PYTHON AND PLANEX_CORE)
    # the version the core is tested with: pybind11's overload resolution changed between 3.0 and 3.1
    find_package(pybind11 3.1 CONFIG REQUIRED)
endif()

# the Python modules link the static libraries; a C++ project may build them shared, on Linux and macOS: every symbol
# exported. On Windows the variables planex's headers define (config::state, ...) would be one copy in the DLL and
# another in the program using it
if(BUILD_SHARED_LIBS AND NOT PLANEX_PYTHON)
    if(WIN32)
        message(FATAL_ERROR "planex: BUILD_SHARED_LIBS is not supported on Windows, build the static libraries: the "
                            "variables planex's headers define, such as config::state, would not be shared between a DLL "
                            "and the program using it")
    endif()
    set(planex_library SHARED)
else()
    set(planex_library STATIC)
endif()

# a binding file takes up to 1.4 GB to compile (measured 2026-10-10); 3 GB per compile leaves room for everything else
# running, more parallel compiles than the memory holds swap and take many times longer
cmake_host_system_information(RESULT PLANEX_MEMORY_MB QUERY TOTAL_PHYSICAL_MEMORY)
math(EXPR PLANEX_COMPILE_JOBS "${PLANEX_MEMORY_MB} / 3072")
if(PLANEX_COMPILE_JOBS LESS 1)
    set(PLANEX_COMPILE_JOBS 1)
endif()
set_property(GLOBAL APPEND PROPERTY JOB_POOLS planex_compile=${PLANEX_COMPILE_JOBS})

function(planex_flags name)
    # pybind11's rule for extension modules: no symbol of one binds in another (with GCC the statics of inline
    # functions, pybind11's own among them, are otherwise one per process); the core and the OCC bindings can be built
    # with different pybind11
    set_target_properties(${name} PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
                          POSITION_INDEPENDENT_CODE ON)
    set_property(TARGET ${name} PROPERTY JOB_POOL_COMPILE planex_compile)
    # netgen's flags allow FMA; GCC fuses a*b + c across statements by default, clang only within one expression, and
    # the results differ in the last bits. GCC before 14 reads "on" as "off"
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${name} PRIVATE -ffp-contract=on)
    endif()
    # each function in a section of its own, so that a module linking the static libraries keeps only what it uses
    if(NOT MSVC)
        target_compile_options(${name} PRIVATE -ffunction-sections -fdata-sections)
    endif()
    # a binding file has more sections than an MSVC object file holds by default; the sources are UTF-8, whatever the
    # code page of the machine that builds; Visual Studio's generator compiles a target's files one after the other
    # unless /MP, here as many at a time as the memory holds. Since Visual Studio 2022 17.10 a std::mutex built by the
    # compiler crashes on its first lock when an older msvcp140.dll is already loaded in the Python process
    if(MSVC)
        target_compile_options(${name} PRIVATE /bigobj /utf-8 /MP${PLANEX_COMPILE_JOBS})
        target_compile_definitions(${name} PRIVATE _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
    endif()
endfunction()

# the CPU flags netgen gives the OCC bindings, so that the core computes the same numbers; a macOS build for given
# architectures (universal2 on an arm64 machine) gives them to its x86-64 part only, as netgen does
function(planex_cpu_flags name)
    if(APPLE AND CMAKE_OSX_ARCHITECTURES)
        if("x86_64" IN_LIST CMAKE_OSX_ARCHITECTURES)
            target_compile_options(${name} PRIVATE "SHELL:-Xarch_x86_64 -march=core-avx2")
        endif()
    elseif(MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64")
        target_compile_options(${name} PRIVATE /arch:AVX2)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        target_compile_options(${name} PRIVATE -march=core-avx2)
    endif()
endfunction()

file(GLOB planex_sources CONFIGURE_DEPENDS src/core/*.cpp src/curves/*.cpp src/wire/*.cpp src/shape/*.cpp src/boolean/*.cpp
     src/offset/*.cpp src/io/*.cpp src/sketch/*.cpp)
if(PLANEX_CONSTRAINTS)
    file(GLOB planex_constraint_sources CONFIGURE_DEPENDS src/constraint_sketch/*.cpp)
    list(APPEND planex_sources ${planex_constraint_sources})
endif()
add_library(planex2d ${planex_library} ${planex_sources})
add_library(planex2d::planex2d ALIAS planex2d)
planex_flags(planex2d)
planex_cpu_flags(planex2d)
target_include_directories(planex2d PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
if(PLANEX_THREADS)
    target_include_directories(planex2d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external_dependencies/taskflow)
    target_compile_definitions(planex2d PUBLIC PLANEX_THREADS)
endif()
if(PLANEX_CONSTRAINTS)
    # Eigen 3.4.0, the submodule only (THIRD_PARTY_NOTICES.md): a system include, so its headers are exempt from our
    # warnings (shadowing is an error with GCC); EIGEN_MPL2_ONLY makes including any of its LGPL parts a compile error
    target_include_directories(planex2d SYSTEM PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external_dependencies/eigen>)
    target_compile_definitions(planex2d PUBLIC PLANEX_CONSTRAINTS EIGEN_MPL2_ONLY)
    # an installed planex: the program's own Eigen 3.4
    target_link_libraries(planex2d PUBLIC $<INSTALL_INTERFACE:Eigen3::Eigen>)
endif()
target_link_libraries(planex2d PUBLIC Threads::Threads)

if(PLANEX_WITH_OCC)
    file(GLOB planex_occ_sources CONFIGURE_DEPENDS src/integrations/netgenocc/*.cpp)
    add_library(planex2d_occ ${planex_library} ${planex_occ_sources})
    add_library(planex2d::planex2d_occ ALIAS planex2d_occ)
    planex_flags(planex2d_occ)
    target_include_directories(planex2d_occ PUBLIC $<BUILD_INTERFACE:${OpenCASCADE_INCLUDE_DIR}>)
    target_compile_definitions(planex2d_occ PUBLIC OCCGEOMETRY)
    target_link_libraries(planex2d_occ PUBLIC planex2d nglib ngcore ${NETGEN_OCC_LIBRARIES})
endif()

if(planex_library STREQUAL "SHARED")
    set(planex_shared planex2d)
    if(PLANEX_WITH_OCC)
        list(APPEND planex_shared planex2d_occ)
    endif()
    set_target_properties(${planex_shared} PROPERTIES CXX_VISIBILITY_PRESET default VISIBILITY_INLINES_HIDDEN OFF)
endif()

# kind core: pybind11 from pip (any 3.x); kind occ: netgen's pybind11 and the flags of its ngcore, so that the OCC
# bindings share netgen's type registry. When the core's registry is another one, the core's objects reach the OCC
# bindings through pybind11's conduit (binding.hpp: IsA, Shadow, ToCore); only the OCC bindings link netgen and OCC
function(planex_module name kind)
    Python3_add_library(${name} MODULE WITH_SOABI ${ARGN})
    planex_flags(${name})
    if(APPLE)
        target_link_options(${name} PRIVATE -Wl,-dead_strip)
    elseif(NOT MSVC)
        target_link_options(${name} PRIVATE -Wl,--gc-sections)
    endif()
    target_link_libraries(${name} PRIVATE planex2d)
    if(PLANEX_THREADS)
        target_include_directories(${name} PRIVATE external_dependencies/taskflow)
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
        planex_cpu_flags(${name})
    endif()
    target_compile_definitions(${name} PRIVATE PLANEX_VERSION="${PLANEX_VERSION}")
    target_link_libraries(${name} PRIVATE Threads::Threads)
    if(kind STREQUAL "occ")
        install(TARGETS ${name} LIBRARY DESTINATION ${PLANEX_OCC_DESTINATION})
    else()
        install(TARGETS ${name} LIBRARY DESTINATION planex)
    endif()
endfunction()

if(PLANEX_PYTHON AND PLANEX_CORE)
    planex_module(_planex core
        src/python/bindings/module.cpp
        src/python/bindings/binding.cpp
        src/python/bindings/bindingdocs.cpp
        src/python/bindings/bindingprops.cpp
        src/python/bindings/bindingargs.cpp
        src/python/bindings/bindingmotion.cpp
        src/python/bindings/bindingmeasure.cpp
        src/python/bindings/bindingsketchdocs.cpp
        src/python/bindings/bindingsketchargs.cpp
        src/python/bindings/bindingsketchnew.cpp
        src/python/bindings/bindingsketchdraw.cpp
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

if(PLANEX_PYTHON AND PLANEX_WITH_OCC)
    planex_module(_planex_occ occ
        src/python/bindings/module_occ.cpp
        src/python/bindings/binding.cpp
        src/python/bindings/bindingdocs.cpp
        src/python/bindings/bindingprops.cpp
        src/python/bindings/bindingargs.cpp
        src/python/bindings/bindingmotion.cpp
        src/python/bindings/bindingmeasure.cpp
        src/python/bindings/bindingsketchdocs.cpp
        src/python/bindings/bindingsketchargs.cpp
        src/python/bindings/bindingsketchnew.cpp
        src/python/bindings/bindingsketchdraw.cpp
        src/python/bindings/binding_occ.cpp
        src/python/bindings/binding_sketch_occ.cpp
        src/python/bindings/binding_section.cpp)
    target_link_libraries(_planex_occ PRIVATE planex2d_occ)
    set_target_properties(_planex_occ PROPERTIES BUILD_RPATH "${NETGEN_LIBRARY_DIR};${OpenCASCADE_LIBRARY_DIR}")
    if(PLANEX_CORE)
        # a development build: the libraries where this machine has them
        set_target_properties(_planex_occ PROPERTIES INSTALL_RPATH "${NETGEN_LIBRARY_DIR};${OpenCASCADE_LIBRARY_DIR}")
        # the netgen the bindings are compiled against, from pip's metadata (empty without it): under another one planex
        # leaves them unloaded and says how to build them again
        execute_process(COMMAND ${Python3_EXECUTABLE} -c
                "import importlib.metadata as m; print(m.version('netgen-mesher'))"
            OUTPUT_VARIABLE planex_netgen_version OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/_occbuild.py
             "# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall\n# SPDX-License-Identifier: LGPL-2.1-or-later\n\n"
             "# the netgen-mesher version this planex's OCC bindings are compiled against\n"
             "NETGEN = \"${planex_netgen_version}\"\n")
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/_occbuild.py DESTINATION planex)
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

# a C++ project: cmake --install puts the headers, the libraries and planex2dConfig.cmake into the prefix, for
# find_package(planex2d); the version is the package's, matched exactly (the C++ interface keeps no compatibility)
if(NOT PLANEX_PYTHON)
    include(CMakePackageConfigHelpers)
    file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/pyproject.toml planex_version_line REGEX "^version = ")
    string(REGEX MATCH "[0-9]+(\\.[0-9]+)*" planex_cmake_version "${planex_version_line}")
    set(planex_targets planex2d)
    if(PLANEX_WITH_OCC)
        list(APPEND planex_targets planex2d_occ)
    endif()
    install(TARGETS ${planex_targets} EXPORT planex2dTargets)
    install(DIRECTORY include/planex DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(EXPORT planex2dTargets NAMESPACE planex2d:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/planex2d)
    file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/planex2dConfig.cmake CONTENT [[
include(CMakeFindDependencyMacro)
find_dependency(Threads)
if(@PLANEX_CONSTRAINTS@)
    find_dependency(Eigen3 3.4)
endif()
if(@PLANEX_WITH_OCC@)
    find_dependency(Netgen CONFIG)
    find_dependency(OpenCASCADE CONFIG)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/planex2dTargets.cmake")
if(@PLANEX_WITH_OCC@)
    set_property(TARGET planex2d::planex2d_occ APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${OpenCASCADE_INCLUDE_DIR}")
endif()
]] @ONLY)
    write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/planex2dConfigVersion.cmake VERSION ${planex_cmake_version}
                                     COMPATIBILITY ExactVersion)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/planex2dConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/planex2dConfigVersion.cmake
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/planex2d)
endif()
