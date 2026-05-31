if(NOT MODERN_LOG_BUILD_EXAMPLES)
    return()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wno-deprecated-declarations)
endif()

add_custom_target(modern_log_examples)

function(modern_log_add_example target_name source_file)
    add_executable(${target_name}
        ${source_file}
    )

    target_compile_features(${target_name} PRIVATE cxx_std_23)
    target_link_libraries(${target_name} PRIVATE modern_log::modern_log)

    add_dependencies(modern_log_examples ${target_name})
endfunction()

modern_log_add_example(modern_log_example_structured_asset_pipeline structured_asset_pipeline.cpp)
modern_log_add_example(modern_log_example_async_runtime_trace async_runtime_trace.cpp)
modern_log_add_example(modern_log_example_dual_channel_output dual_channel_output.cpp)
modern_log_add_example(modern_log_example_custom_context_injection custom_context_injection.cpp)
modern_log_add_example(modern_log_example_rotating_file_sink rotating_file_sink.cpp)
modern_log_add_example(modern_log_example_remote_sink_bridge remote_sink_bridge.cpp)
modern_log_add_example(modern_log_example_otel_sink_bridge otel_sink_bridge.cpp)