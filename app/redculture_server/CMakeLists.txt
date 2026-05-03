add_library(redculture_server_app STATIC)
add_library(RedCulture::server_app ALIAS redculture_server_app)

target_sources(redculture_server_app
    PRIVATE
        src/api/http_utils.cpp
        src/api/server_routes.cpp
        src/api/controllers/auth_controller.cpp
        src/api/controllers/interaction_controller.cpp
        src/api/controllers/ops_controller.cpp
        src/api/controllers/room_controller.cpp
        src/api/controllers/tts_controller.cpp
        src/application/service_application.cpp
)

target_include_directories(redculture_server_app
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(redculture_server_app
    PUBLIC
        RedCulture::service
)

add_executable(redculture_server
    main.cpp
)

target_link_libraries(redculture_server
    PRIVATE
        RedCulture::server_app
)

add_custom_command(TARGET redculture_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:redculture_server>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/config/app.yaml"
            "$<TARGET_FILE_DIR:redculture_server>/config/app.yaml"
)
