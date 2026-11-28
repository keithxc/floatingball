cmake_minimum_required(VERSION 3.16)

project(FloatingBall VERSION 0.2 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets LinguistTools Network)

if(APPLE AND NOT IOS)
    enable_language(OBJCXX)
endif()

set(TS_FILES FloatingBall_zh_CN.ts)

set(FLOATINGBALL_CORE_SOURCES
    floatingball.cpp
    floatingball.h
    floatingball.ui
    clipboardbubble.cpp
    clipboardbubble.h
    quickdeck.cpp
    quickdeck.h
    systempopover.cpp
    systempopover.h
    applauncher.cpp
    applauncher.h
    floatingwindow.cpp
    floatingwindow.h
    touchscroll.h
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
    target_link_libraries(${target} PRIVATE Qt6::Widgets Qt6::Network)
    target_sources(${target} PRIVATE appdock.cpp appdock.h systemstatus.cpp systemstatus.h desktopactions.cpp desktopactions.h panelbackdrop.cpp panelbackdrop.h)

    if(APPLE AND NOT IOS)
        target_sources(${target} PRIVATE
            appdock_mac.mm
            appdock_ax_mac.mm
            appdock_windows_mac.mm
            systemstatus_mac.mm
            systemstatus_notif_mac.mm
            systemstatus_sensors_mac.mm
            panelbackdrop_mac.mm
            desktopactions_mac.mm
        )
        target_link_libraries(${target} PRIVATE
            "-framework AppKit"
            "-framework ApplicationServices"
            "-framework AudioToolbox"
            "-framework Carbon"
            "-framework CoreAudio"
            "-framework CoreGraphics"
            "-framework CoreMediaIO"
            "-framework CoreWLAN"
            "-framework Foundation"
            "-framework IOBluetooth"
            "-framework IOKit"
            "-framework QuartzCore"
            sqlite3
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
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(WAYLAND_CLIENT QUIET IMPORTED_TARGET wayland-client)
        endif()
        if(TARGET PkgConfig::WAYLAND_CLIENT)
            find_package(Qt6 QUIET COMPONENTS GuiPrivate)
            target_link_libraries(${target} PRIVATE PkgConfig::WAYLAND_CLIENT)
            target_compile_definitions(${target} PRIVATE FLOATINGBALL_WAYLAND_CLIENT)
            if(TARGET Qt6::GuiPrivate)
                target_link_libraries(${target} PRIVATE Qt6::GuiPrivate)
                target_compile_definitions(${target} PRIVATE FLOATINGBALL_QT_GUI_PRIVATE)
            endif()
        endif()
    endif()

    if(WIN32)
        target_link_libraries(${target} PRIVATE shell32 ole32 uuid user32 gdi32)
    endif()
endfunction()

floatingball_configure_common(FloatingBall)

set_target_properties(FloatingBall PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "dev.floatingball.FloatingBall"
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

option(FLOATINGBALL_BUILD_TESTS "Build touch radial, control center, launcher and platform checks" OFF)
if(FLOATINGBALL_BUILD_TESTS)
    enable_testing()

    add_executable(floatingball-touch-ux-test
        tests/touch-ux-test.cpp
        ${FLOATINGBALL_CORE_SOURCES}
    )
    floatingball_configure_common(floatingball-touch-ux-test)
    add_test(NAME touch-ux-workflow
             COMMAND floatingball-touch-ux-test "${CMAKE_CURRENT_BINARY_DIR}/touch-ux-data")
    set_tests_properties(touch-ux-workflow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    if(APPLE AND NOT IOS)
        add_executable(floatingball-macos-parity-test
            tests/macos-parity-test.mm
            ${FLOATINGBALL_CORE_SOURCES}
        )
        floatingball_configure_common(floatingball-macos-parity-test)
        add_test(NAME macos-parity-workflow
                 COMMAND floatingball-macos-parity-test "${CMAKE_CURRENT_BINARY_DIR}/macos-parity-data")
        set_tests_properties(macos-parity-workflow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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
