add_executable(rcs_ai_orchestrator_example
    main.cpp
)

target_link_libraries(rcs_ai_orchestrator_example
    PRIVATE
        RedCulture::service
)
