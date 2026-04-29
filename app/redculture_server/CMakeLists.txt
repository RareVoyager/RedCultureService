add_executable(redculture_server
    main.cpp
)

target_link_libraries(redculture_server
    PRIVATE
        RedCulture::service
)
