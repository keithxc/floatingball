cmake_minimum_required(VERSION 3.16)

project(FloatingBall VERSION 0.2 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets LinguistTools Multimedia)

if(APPLE AND NOT IOS)
    enable_language(OBJCXX)
endif()

set(TS_FILES FloatingBall_zh_CN.ts)

set(FLOATINGBALL_CORE_SOURCES
    floatingball.cpp
    floatingball.h
    floatingball.ui
    actionhud.cpp
    actionhud.h
    clipboardbubble.cpp
    clipboardbubble.h
    quickdeck.cpp
    quickdeck.h
    screenshotpanel.cpp
    screenshotpanel.h
    screenshotstore.cpp
    screenshotstore.h
    screenshotcapture.cpp
    screenshotcapture.h
    screenrecorder.cpp
    screenrecorder.h
    floatingwindow.cpp
    floatingwindow.h
    uiicons.h
)

qt_add_executable(FloatingBall MANUAL_FINALIZATION
    main.cpp
    ${FLOATINGBALL_CORE_SOURCES}
    ${TS_FILES}
)
qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})

function(floatingball_configure_common target)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${target} PRIVATE Qt6::Widgets Qt6::Multimedia)
    target_sources(${target} PRIVATE appdock.cpp appdock.h systemstatus.cpp systemstatus.h desktopactions.cpp desktopactions.h screentext.cpp screentext.h panelbackdrop.cpp panelbackdrop.h)

    if(APPLE AND NOT IOS)
        target_sources(${target} PRIVATE
            appdock_mac.mm
            systemstatus_mac.mm
            screentext_mac.mm
            panelbackdrop_mac.mm
            desktopactions_mac.mm
        )
        target_link_libraries(${target} PRIVATE
            "-framework AppKit"
            "-framework ApplicationServices"
            "-framework Carbon"
            "-framework CoreGraphics"
            "-framework Foundation"
            "-framework ImageIO"
            "-framework IOKit"
            "-framework QuartzCore"
            "-framework Vision"
        )
    endif()

    if(UNIX AND NOT APPLE)
        find_package(Qt6 REQUIRED COMPONENTS DBus)
        target_link_libraries(${target} PRIVATE Qt6::DBus)
        find_package(LayerShellQt 6.6 QUIET)
        if(TARGET LayerShellQt::Interface)
            target_link_libraries(${target} PRIVATE LayerShellQt::Interface)
            target_compile_definitions(${target} PRIVATE FLOATINGBALL_LAYER_SHELL)
        endif()
    endif()

    if(WIN32)
        target_link_libraries(${target} PRIVATE shell32 ole32 uuid user32 gdi32)
    endif()
endfunction()

floatingball_configure_common(FloatingBall)

set_target_properties(FloatingBall PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
include(cmake/AppIcon.cmake)
install(TARGETS FloatingBall
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(FloatingBall)

option(FLOATINGBALL_BUILD_TESTS "Build touch radial, crosshair, HUD and platform checks" OFF)
if(FLOATINGBALL_BUILD_TESTS)
    enable_testing()

    add_executable(floatingball-screenshot-test
        tests/screenshot-test.cpp
        ${FLOATINGBALL_CORE_SOURCES}
    )
    floatingball_configure_common(floatingball-screenshot-test)
    if(APPLE AND NOT IOS)
        target_sources(floatingball-screenshot-test PRIVATE tests/native-window-check.mm)
    endif()
    add_test(NAME screenshot-integration
             COMMAND floatingball-screenshot-test "${CMAKE_CURRENT_BINARY_DIR}/test-data")
    set_tests_properties(screenshot-integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(floatingball-capture-test
        tests/capture-test.cpp
        screenshotcapture.cpp screenshotcapture.h
        screenshotpanel.cpp screenshotpanel.h
        screenshotstore.cpp screenshotstore.h
        screenrecorder.cpp screenrecorder.h
        floatingwindow.cpp floatingwindow.h
    )
    floatingball_configure_common(floatingball-capture-test)
    add_test(NAME capture-workflow
             COMMAND floatingball-capture-test "${CMAKE_CURRENT_BINARY_DIR}/test-data")
    set_tests_properties(capture-workflow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(floatingball-touch-ux-test
        tests/touch-ux-test.cpp
        ${FLOATINGBALL_CORE_SOURCES}
    )
    floatingball_configure_common(floatingball-touch-ux-test)
    add_test(NAME touch-ux-workflow
             COMMAND floatingball-touch-ux-test "${CMAKE_CURRENT_BINARY_DIR}/touch-ux-data")
    set_tests_properties(touch-ux-workflow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(floatingball-native-capture-check
        tests/native-capture-check.cpp
        screenshotcapture.cpp screenshotcapture.h
        screenshotpanel.cpp screenshotpanel.h
        screenshotstore.cpp screenshotstore.h
        screenrecorder.cpp screenrecorder.h
        floatingwindow.cpp floatingwindow.h
    )
    floatingball_configure_common(floatingball-native-capture-check)

    if(APPLE AND NOT IOS)
        add_executable(floatingball-backdrop-check MACOSX_BUNDLE
            tests/backdrop-check.mm
            floatingwindow.cpp floatingwindow.h
            panelbackdrop.cpp panelbackdrop.h
            panelbackdrop_mac.mm
        )
        target_include_directories(floatingball-backdrop-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(floatingball-backdrop-check PRIVATE
            Qt6::Widgets
            "-framework AppKit"
            "-framework QuartzCore"
        )
        set_target_properties(floatingball-backdrop-check PROPERTIES
            MACOSX_BUNDLE_GUI_IDENTIFIER "dev.floatingball.BackdropCheck"
        )
    endif()
endif()
