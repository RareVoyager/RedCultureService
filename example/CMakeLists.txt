add_subdirectory(3rdpatryexample)

add_executable(rcs_example
    main.cpp
)

target_link_libraries(rcs_example
    PRIVATE
        RedCulture::service
)
