add_executable(rcs_ops_example
    main.cpp
)

target_link_libraries(rcs_ops_example
    PRIVATE
        RedCulture::service
)
