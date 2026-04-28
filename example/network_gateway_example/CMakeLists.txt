add_executable(rcs_network_gateway_example
    main.cpp
)

target_link_libraries(rcs_network_gateway_example
    PRIVATE
        RedCulture::service
)
