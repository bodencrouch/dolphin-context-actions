cmake_minimum_required(VERSION 3.20)
project(dolphin_link_fileitem_action LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ECM 6.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs6)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(CTest)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
find_package(KF6 6.0 REQUIRED COMPONENTS CoreAddons KIO)

kcoreaddons_add_plugin(
    dolphinlinkfileitemaction
    SOURCES dolphinlinkfileitemaction.cpp
    INSTALL_NAMESPACE "kf6/kfileitemaction"
)
target_link_libraries(
    dolphinlinkfileitemaction
    PRIVATE
        Qt6::Widgets
        KF6::CoreAddons
        KF6::KIOWidgets
)

if(BUILD_TESTING)
    add_executable(link_menu_probe ../tests/link_menu_probe.cpp)
    target_link_libraries(
        link_menu_probe
        PRIVATE
            Qt6::Widgets
            KF6::KIOWidgets
    )
    add_test(
        NAME link_menu_state
        COMMAND
            ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_plugin_menu.sh
            ${CMAKE_CURRENT_BINARY_DIR}
    )
endif()
