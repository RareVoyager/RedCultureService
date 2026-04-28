add_subdirectory(3rdpatryexample)
add_subdirectory(network_gateway_example)

add_executable(rcs_example
    main.cpp
)

target_link_libraries(rcs_example
    PRIVATE
        RedCulture::service
)
