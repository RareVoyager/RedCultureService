add_subdirectory(3rdpatryexample)
add_subdirectory(network_gateway_example)
add_subdirectory(auth_example)
add_subdirectory(room_match_example)
add_subdirectory(state_sync_example)
add_subdirectory(ai_orchestrator_example)
add_subdirectory(voice_tts_example)
add_subdirectory(storage_example)
add_subdirectory(config_hotreload_example)
add_subdirectory(observability_example)
add_subdirectory(ops_example)

add_executable(rcs_example
    main.cpp
)

target_link_libraries(rcs_example
    PRIVATE
        RedCulture::service
)
