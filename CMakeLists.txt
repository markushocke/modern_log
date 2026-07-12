cmake_minimum_required(VERSION 3.28)

project(modern_log VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MODERN_LOG_BUILD_TESTS "Build bootstrap test targets" ON)
option(MODERN_LOG_BUILD_EXAMPLES "Build bootstrap example targets" ON)
option(MODERN_LOG_BUILD_BENCHMARKS "Build bootstrap benchmark targets" ON)

include(FetchContent)

set(MODERN_LOG_DEPENDENCY_DIR "${CMAKE_BINARY_DIR}/dependencies" CACHE PATH
    "Directory for modern_log dependency Git checkouts")
set(MODERN_TRACE_GIT_REPOSITORY "https://github.com/markushocke/modern_trace.git" CACHE STRING
    "Git repository for modern_trace")
set(MODERN_TRACE_GIT_TAG "main" CACHE STRING "Git tag or branch for modern_trace")
set(MODERN_RUNTIME_GIT_REPOSITORY "https://github.com/markushocke/modern_runtime.git" CACHE STRING
    "Git repository for modern_runtime")
set(MODERN_RUNTIME_GIT_TAG "main" CACHE STRING "Git tag or branch for modern_runtime")
set(MODERN_IO_GIT_REPOSITORY "https://github.com/markushocke/modern_io.git" CACHE STRING
    "Git repository for modern_io")
set(MODERN_IO_GIT_TAG "main" CACHE STRING "Git tag or branch for modern_io")

function(modern_log_fetch_dependency package_name repository tag)
    if(TARGET ${package_name} OR TARGET ${package_name}::${package_name})
        return()
    endif()

    message(STATUS "modern_log: checking out ${package_name} in ${MODERN_LOG_DEPENDENCY_DIR}/${package_name}")
    FetchContent_Declare(
        ${package_name}
        GIT_REPOSITORY "${repository}"
        GIT_TAG "${tag}"
        GIT_SHALLOW TRUE
        SOURCE_DIR "${MODERN_LOG_DEPENDENCY_DIR}/${package_name}"
        BINARY_DIR "${CMAKE_BINARY_DIR}/dependencies-build/${package_name}"
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(${package_name})
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

modern_log_fetch_dependency(modern_trace "${MODERN_TRACE_GIT_REPOSITORY}" "${MODERN_TRACE_GIT_TAG}")
modern_log_fetch_dependency(modern_runtime "${MODERN_RUNTIME_GIT_REPOSITORY}" "${MODERN_RUNTIME_GIT_TAG}")
modern_log_fetch_dependency(modern_io "${MODERN_IO_GIT_REPOSITORY}" "${MODERN_IO_GIT_TAG}")
FetchContent_GetProperties(modern_io)
modern_log_add_test_include_root("${modern_io_SOURCE_DIR}" "${modern_io_SOURCE_DIR}")

modern_log_pick_dependency_target(MODERN_LOG_TRACE_TARGET modern_trace::modern_trace modern_trace)
modern_log_pick_dependency_target(MODERN_LOG_RUNTIME_TARGET modern_runtime::modern_runtime modern_runtime)
modern_log_pick_dependency_target(MODERN_LOG_IO_TARGET modern_io::modern_io modern_io)

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
        ${MODERN_LOG_TRACE_TARGET}
        ${MODERN_LOG_RUNTIME_TARGET}
        ${MODERN_LOG_IO_TARGET}
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Clang + libstdc++ 13 emits deprecated aligned_storage warnings from
    # standard-library internals such as std::make_shared and std::async.
    # Suppress this toolchain noise while keeping the project build usable.
    target_compile_options(modern_log PRIVATE -Wno-deprecated-declarations)
endif()

add_subdirectory(tests)
add_subdirectory(examples)
add_subdirectory(benchmarks)

if(MODERN_LOG_BUILD_TESTS)
    modern_log_disable_vendored_tests("${CMAKE_CURRENT_SOURCE_DIR}")
endif()
