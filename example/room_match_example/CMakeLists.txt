add_executable(rcs_room_match_example
    main.cpp
)

target_link_libraries(rcs_room_match_example
    PRIVATE
        RedCulture::service
)
