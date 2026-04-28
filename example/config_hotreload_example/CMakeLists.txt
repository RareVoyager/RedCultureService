add_executable(rcs_config_hotreload_example
    main.cpp
)

target_link_libraries(rcs_config_hotreload_example
    PRIVATE
        RedCulture::service
)
