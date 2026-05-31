cmake_minimum_required(VERSION 3.28)

project(modern_log VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MODERN_LOG_BUILD_TESTS "Build bootstrap test targets" ON)
option(MODERN_LOG_BUILD_EXAMPLES "Build bootstrap example targets" ON)
option(MODERN_LOG_BUILD_BENCHMARKS "Build bootstrap benchmark targets" ON)

function(modern_log_resolve_dependency package_name namespaced_target plain_target)
    if(TARGET ${namespaced_target} OR TARGET ${plain_target})
        return()
    endif()

    set(binary_checkout "${CMAKE_CURRENT_BINARY_DIR}/deps/${package_name}")
    set(sibling_checkout "${CMAKE_CURRENT_SOURCE_DIR}/../${package_name}")

    if(EXISTS "${binary_checkout}/CMakeLists.txt")
        message(STATUS "modern_log: using ${package_name} from ${binary_checkout}")
        add_subdirectory("${binary_checkout}" "${CMAKE_CURRENT_BINARY_DIR}/extern/${package_name}" EXCLUDE_FROM_ALL)
    elseif(EXISTS "${sibling_checkout}/CMakeLists.txt")
        message(STATUS "modern_log: using ${package_name} from ${sibling_checkout}")
        add_subdirectory("${sibling_checkout}" "${CMAKE_CURRENT_BINARY_DIR}/extern/${package_name}" EXCLUDE_FROM_ALL)
    else()
        message(STATUS "modern_log: resolving ${package_name} via find_package")
        find_package(${package_name} CONFIG REQUIRED)
    endif()
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

modern_log_resolve_dependency(modern_trace modern_trace::modern_trace modern_trace)
modern_log_resolve_dependency(modern_runtime modern_runtime::modern_runtime modern_runtime)
modern_log_resolve_dependency(modern_io modern_io::modern_io modern_io)

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

add_subdirectory(tests)
add_subdirectory(examples)
add_subdirectory(benchmarks)

if(MODERN_LOG_BUILD_TESTS)
    modern_log_disable_vendored_tests("${CMAKE_CURRENT_SOURCE_DIR}")
endif()