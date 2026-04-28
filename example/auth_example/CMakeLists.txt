add_executable(rcs_auth_example
    main.cpp
)

target_link_libraries(rcs_auth_example
    PRIVATE
        RedCulture::service
)
