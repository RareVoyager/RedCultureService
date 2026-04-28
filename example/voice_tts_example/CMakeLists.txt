add_executable(rcs_voice_tts_example
    main.cpp
)

target_link_libraries(rcs_voice_tts_example
    PRIVATE
        RedCulture::service
)
