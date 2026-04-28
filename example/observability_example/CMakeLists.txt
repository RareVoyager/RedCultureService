add_executable(rcs_observability_example
    main.cpp
)

target_link_libraries(rcs_observability_example
    PRIVATE
        RedCulture::service
)
