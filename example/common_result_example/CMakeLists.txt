add_executable(rcs_common_result_example
    main.cpp
)

target_link_libraries(rcs_common_result_example
    PRIVATE
        RedCulture::common
)
