add_library(redculture_service)
add_library(RedCulture::service ALIAS redculture_service)

target_sources(redculture_service
    PRIVATE
        common/service.cpp
        net/message.cpp
        net/rate_limiter.cpp
        net/tcp_connection.cpp
        net/network_gateway.cpp
        auth/session_auth_service.cpp
        room/room_match_service.cpp
        sync/state_sync_service.cpp
        ai_orchestrator/ai_orchestrator_service.cpp
        voice_tts/voice_tts_service.cpp
        storage/storage_service.cpp
        config_hotreload/config_hotreload_service.cpp
        observability/telemetry_service.cpp
        ops/ops_service.cpp
)

target_include_directories(redculture_service
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(redculture_service
    PUBLIC
        ${RCS_BOOST_TARGET}
        spdlog::spdlog_header_only
        nlohmann_json::nlohmann_json
        yaml-cpp::yaml-cpp
)

if(RCS_WITH_GRPC)
    target_link_libraries(redculture_service
        PUBLIC
            gRPC::grpc++
            protobuf::libprotobuf
    )
    target_compile_definitions(redculture_service PUBLIC RCS_WITH_GRPC=1)
endif()
