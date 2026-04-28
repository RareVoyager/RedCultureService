add_executable(rcs_storage_example
    main.cpp
)

target_link_libraries(rcs_storage_example
    PRIVATE
        RedCulture::service
)
