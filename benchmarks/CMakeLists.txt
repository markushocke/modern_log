if(NOT MODERN_LOG_BUILD_BENCHMARKS)
    return()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wno-deprecated-declarations)
endif()

add_custom_target(modern_log_benchmarks)

add_executable(modern_log_hot_path_benchmark
    hot_path_benchmark.cpp
)

target_compile_features(modern_log_hot_path_benchmark PRIVATE cxx_std_23)
target_link_libraries(modern_log_hot_path_benchmark PRIVATE modern_log::modern_log)

add_dependencies(modern_log_benchmarks modern_log_hot_path_benchmark)