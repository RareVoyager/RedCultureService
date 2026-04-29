add_executable(rcs_http_api_example
    main.cpp
)

target_link_libraries(rcs_http_api_example
    PRIVATE
        RedCulture::service
)
