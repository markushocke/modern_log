cmake_minimum_required(VERSION 3.28)

project(modern_log VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MODERN_LOG_BUILD_TESTS "Build bootstrap test targets" ON)
option(MODERN_LOG_BUILD_EXAMPLES "Build bootstrap example targets" ON)
option(MODERN_LOG_BUILD_BENCHMARKS "Build bootstrap benchmark targets" ON)

set(MODERN_LOG_DEPENDENCY_DIR "${CMAKE_CURRENT_BINARY_DIR}/dependencies" CACHE PATH
    "Directory for modern_log dependency Git checkouts")
set(MODERN_TRACE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../modern_trace" CACHE PATH "Local modern_trace checkout")
set(MODERN_RUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../modern_runtime" CACHE PATH "Local modern_runtime checkout")
set(MODERN_IO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../modern_io" CACHE PATH "Local modern_io checkout")
set(MODERN_TRACE_GIT_REPOSITORY "https://github.com/markushocke/modern_trace.git" CACHE STRING
    "Git repository for modern_trace")
set(MODERN_TRACE_GIT_TAG "f9a9250a0275d4a0a67a6dc751dec67e377a0c2a" CACHE STRING "Pinned modern_trace revision")
set(MODERN_RUNTIME_GIT_REPOSITORY "https://github.com/markushocke/modern_runtime.git" CACHE STRING
    "Git repository for modern_runtime")
set(MODERN_RUNTIME_GIT_TAG "f1fe65b65889d9d13ee2e232c7129455bf11971f" CACHE STRING "Pinned modern_runtime revision")
set(MODERN_IO_GIT_REPOSITORY "https://github.com/markushocke/modern_io.git" CACHE STRING
    "Git repository for modern_io")
set(MODERN_IO_GIT_TAG "e0bd2fb10510ab800818b300239f7a1919e5bd7a" CACHE STRING "Pinned modern_io revision")

function(modern_log_resolve_dependency package_name namespaced_target local_root repository tag out_target)
    if(TARGET ${namespaced_target})
        set(${out_target} ${namespaced_target} PARENT_SCOPE)
        return()
    endif()

    if(EXISTS "${local_root}/CMakeLists.txt")
        message(STATUS "modern_log: using local ${package_name} checkout at ${local_root}")
        add_subdirectory("${local_root}" "${CMAKE_CURRENT_BINARY_DIR}/dependencies-build/${package_name}" EXCLUDE_FROM_ALL)
    else()
        find_package(${package_name} CONFIG QUIET)
    endif()

    if(NOT TARGET ${namespaced_target})
        include(FetchContent)
        message(STATUS "modern_log: fetching pinned ${package_name}@${tag}")
        FetchContent_Declare(
            ${package_name}
            GIT_REPOSITORY "${repository}"
            GIT_TAG "${tag}"
            SOURCE_DIR "${MODERN_LOG_DEPENDENCY_DIR}/${package_name}"
            BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/dependencies-build/${package_name}"
            EXCLUDE_FROM_ALL)
        FetchContent_MakeAvailable(${package_name})
    endif()

    if(NOT TARGET ${namespaced_target})
        message(FATAL_ERROR "modern_log: ${package_name} did not provide ${namespaced_target}")
    endif()
    set(${out_target} ${namespaced_target} PARENT_SCOPE)
endfunction()

function(modern_log_add_test_include_root directory_path include_root)
    get_property(directory_targets DIRECTORY "${directory_path}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target_name IN LISTS directory_targets)
        get_target_property(target_source_dir ${target_name} SOURCE_DIR)
        if(target_source_dir MATCHES "^${include_root}/tests(/|$)")
            target_include_directories(${target_name} PRIVATE "${include_root}")
        endif()
    endforeach()

    get_property(subdirectories DIRECTORY "${directory_path}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        modern_log_add_test_include_root("${subdirectory}" "${include_root}")
    endforeach()
endfunction()

function(modern_log_pick_dependency_target out_var namespaced_target plain_target)
    if(TARGET ${namespaced_target})
        set(${out_var} ${namespaced_target} PARENT_SCOPE)
    elseif(TARGET ${plain_target})
        set(${out_var} ${plain_target} PARENT_SCOPE)
    else()
        message(FATAL_ERROR "modern_log: dependency target ${namespaced_target} or ${plain_target} is required")
    endif()
endfunction()

function(modern_log_disable_vendored_tests directory_path)
    get_property(directory_tests DIRECTORY "${directory_path}" PROPERTY TESTS)
    foreach(test_name IN LISTS directory_tests)
        if(NOT test_name MATCHES "^modern_log\\.")
            set_property(TEST "${test_name}" DIRECTORY "${directory_path}" PROPERTY DISABLED TRUE)
        endif()
    endforeach()

    get_property(subdirectories DIRECTORY "${directory_path}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        modern_log_disable_vendored_tests("${subdirectory}")
    endforeach()
endfunction()

modern_log_resolve_dependency(modern_trace modern_trace::modern_trace "${MODERN_TRACE_ROOT}"
    "${MODERN_TRACE_GIT_REPOSITORY}" "${MODERN_TRACE_GIT_TAG}" MODERN_LOG_TRACE_TARGET)
modern_log_resolve_dependency(modern_runtime modern_runtime::modern_runtime "${MODERN_RUNTIME_ROOT}"
    "${MODERN_RUNTIME_GIT_REPOSITORY}" "${MODERN_RUNTIME_GIT_TAG}" MODERN_LOG_RUNTIME_TARGET)
modern_log_resolve_dependency(modern_io modern_io::modern_io "${MODERN_IO_ROOT}"
    "${MODERN_IO_GIT_REPOSITORY}" "${MODERN_IO_GIT_TAG}" MODERN_LOG_IO_TARGET)

if(MODERN_LOG_BUILD_TESTS)
    enable_testing()
endif()

add_library(modern_log STATIC)
add_library(modern_log::modern_log ALIAS modern_log)

target_compile_features(modern_log PUBLIC cxx_std_23)

target_sources(modern_log
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
            modules/modern.log.cppm
            modules/core/modern.log.core.cppm
            modules/async/modern.log.async.cppm
            modules/format/modern.log.format.cppm
            modules/sinks/modern.log.sinks.cppm
            modules/context/modern.log.context.cppm
            modules/metrics/modern.log.metrics.cppm
)

target_link_libraries(modern_log
    PUBLIC
        $<BUILD_INTERFACE:${MODERN_LOG_TRACE_TARGET}>
        $<BUILD_INTERFACE:${MODERN_LOG_RUNTIME_TARGET}>
        $<BUILD_INTERFACE:${MODERN_LOG_IO_TARGET}>
        $<INSTALL_INTERFACE:modern_trace::modern_trace>
        $<INSTALL_INTERFACE:modern_runtime::modern_runtime>
        $<INSTALL_INTERFACE:modern_io::modern_io>
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Clang + libstdc++ 13 emits deprecated aligned_storage warnings from
    # standard-library internals such as std::make_shared and std::async.
    # Suppress this toolchain noise while keeping the project build usable.
    target_compile_options(modern_log PRIVATE -Wno-deprecated-declarations)
endif()

if(MODERN_LOG_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(MODERN_LOG_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(MODERN_LOG_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(MODERN_LOG_BUILD_TESTS)
    modern_log_disable_vendored_tests("${CMAKE_CURRENT_SOURCE_DIR}")
endif()

configure_package_config_file(
    cmake/modern_logConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/modern_logConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/modern_log")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/modern_logConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)
configure_file(
    cmake/modern_logTargets.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/modern_logTargets.cmake"
    @ONLY)

install(TARGETS modern_log
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    FILE_SET cxx_modules DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/modern_log")
install(DIRECTORY modules/detail DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/modern_log/modules")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/modern_logConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/modern_logConfigVersion.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/modern_logTargets.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/modern_log")
