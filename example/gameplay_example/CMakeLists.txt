add_executable(rcs_gameplay_example
    main.cpp
)

target_link_libraries(rcs_gameplay_example
    PRIVATE
        RedCulture::service
)
