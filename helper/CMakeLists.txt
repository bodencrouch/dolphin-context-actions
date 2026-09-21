find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Test)

qt_add_executable(dolphin-context-actions
    main.cpp
    cli.cpp
    ui.cpp
    config.cpp
    archive_ops.cpp
    link_ops.cpp
    file_converter.cpp
    file_converter_menus.cpp
    converters.cpp
    uploaders.cpp
)

qt_add_resources(dolphin-context-actions "registry"
    PREFIX "/"
    BASE ${CMAKE_SOURCE_DIR}/assets
    FILES ${CMAKE_SOURCE_DIR}/assets/conversions.json
)

target_include_directories(dolphin-context-actions PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dolphin-context-actions PRIVATE Qt6::Core Qt6::Network)
target_compile_definitions(dolphin-context-actions PRIVATE
    QT_NO_KEYWORDS
    APP_VERSION="${PROJECT_VERSION}"
)

install(TARGETS dolphin-context-actions RUNTIME DESTINATION bin)

if(BUILD_TESTING)
    enable_testing()
    qt_add_executable(helper_unit_tests
        tests/test_helper.cpp
        archive_ops.cpp
        file_converter.cpp
        file_converter_menus.cpp
        ui.cpp
        config.cpp
    )
    qt_add_resources(helper_unit_tests "registry"
        PREFIX "/"
        BASE ${CMAKE_SOURCE_DIR}/assets
        FILES ${CMAKE_SOURCE_DIR}/assets/conversions.json
    )
    target_include_directories(helper_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(helper_unit_tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME helper_unit_tests COMMAND helper_unit_tests)
    add_test(
        NAME helper_cli
        COMMAND ${CMAKE_SOURCE_DIR}/tests/test_helper_cli.sh $<TARGET_FILE:dolphin-context-actions>
    )
    set_tests_properties(helper_cli helper_unit_tests PROPERTIES
        ENVIRONMENT "DOLPHIN_CONTEXT_ACTIONS_HEADLESS=1;QT_QPA_PLATFORM=offscreen"
    )
endif()
