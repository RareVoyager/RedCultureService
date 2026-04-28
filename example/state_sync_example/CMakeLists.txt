add_executable(rcs_state_sync_example
    main.cpp
)

target_link_libraries(rcs_state_sync_example
    PRIVATE
        RedCulture::service
)
