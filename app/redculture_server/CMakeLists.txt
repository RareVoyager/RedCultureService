add_executable(redculture_server
    main.cpp
    server_routes.cpp
    controllers/auth_controller.cpp
)

target_include_directories(redculture_server
    PRIVATE
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(redculture_server
    PRIVATE
        RedCulture::service
)
