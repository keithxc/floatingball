cmake_minimum_required(VERSION 3.16)

project(FloatingBall VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets LinguistTools Multimedia)

set(TS_FILES FloatingBall_zh_CN.ts)

set(PROJECT_SOURCES
        main.cpp
        floatingball.cpp
        screenshotpanel.cpp
        screenshotpanel.h
        screenshotstore.cpp
        screenshotstore.h
        uiicons.h
        floatingwindow.cpp
        floatingwindow.h
        screenshotcapture.cpp
        screenshotcapture.h
        screenrecorder.cpp
        screenrecorder.h
        floatingball.h
        floatingball.ui
        ${TS_FILES}
)

qt_add_executable(FloatingBall MANUAL_FINALIZATION ${PROJECT_SOURCES})
qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})

target_link_libraries(FloatingBall PRIVATE Qt6::Widgets)
target_link_libraries(FloatingBall PRIVATE Qt6::Multimedia)
if(UNIX AND NOT APPLE)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
    target_link_libraries(FloatingBall PRIVATE Qt6::DBus)
endif()
if(UNIX AND NOT APPLE)
    find_package(LayerShellQt 6.6 QUIET)
    if(TARGET LayerShellQt::Interface)
        target_link_libraries(FloatingBall PRIVATE LayerShellQt::Interface)
        target_compile_definitions(FloatingBall PRIVATE FLOATINGBALL_LAYER_SHELL)
    endif()
endif()

if(WIN32)
    target_link_libraries(FloatingBall PRIVATE shell32 ole32 uuid user32 gdi32)
endif()

set_target_properties(FloatingBall PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS FloatingBall
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(FloatingBall)

option(FLOATINGBALL_BUILD_TESTS "Build screenshot and action integration checks" OFF)
if(FLOATINGBALL_BUILD_TESTS)
    enable_testing()
    add_executable(floatingball-screenshot-test
        tests/screenshot-test.cpp floatingball.cpp floatingball.h floatingball.ui
        screenshotpanel.cpp screenshotpanel.h screenshotstore.cpp screenshotstore.h)
    target_sources(floatingball-screenshot-test PRIVATE floatingwindow.cpp floatingwindow.h)
    target_sources(floatingball-screenshot-test PRIVATE screenshotcapture.cpp screenshotcapture.h)
    target_sources(floatingball-screenshot-test PRIVATE screenrecorder.cpp screenrecorder.h)
    target_include_directories(floatingball-screenshot-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(floatingball-screenshot-test PRIVATE Qt6::Widgets)
    target_link_libraries(floatingball-screenshot-test PRIVATE Qt6::Multimedia)
    if(UNIX AND NOT APPLE)
        target_link_libraries(floatingball-screenshot-test PRIVATE Qt6::DBus)
    endif()
    if(TARGET LayerShellQt::Interface)
        target_link_libraries(floatingball-screenshot-test PRIVATE LayerShellQt::Interface)
        target_compile_definitions(floatingball-screenshot-test PRIVATE FLOATINGBALL_LAYER_SHELL)
    endif()
    if(APPLE AND NOT IOS)
        enable_language(OBJCXX)
        target_sources(floatingball-screenshot-test PRIVATE tests/native-window-check.mm)
        target_link_libraries(floatingball-screenshot-test PRIVATE "-framework AppKit")
    endif()
    if(WIN32)
        target_link_libraries(floatingball-screenshot-test PRIVATE shell32 ole32 uuid user32 gdi32)
    endif()
    add_test(NAME screenshot-integration COMMAND floatingball-screenshot-test "${CMAKE_CURRENT_BINARY_DIR}/test-data")
    set_tests_properties(screenshot-integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(floatingball-capture-test tests/capture-test.cpp
        screenshotcapture.cpp screenshotcapture.h screenshotpanel.cpp screenshotpanel.h
        screenshotstore.cpp screenshotstore.h screenrecorder.cpp screenrecorder.h
        floatingwindow.cpp floatingwindow.h)
    target_include_directories(floatingball-capture-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(floatingball-capture-test PRIVATE Qt6::Widgets Qt6::Multimedia)
    if(UNIX AND NOT APPLE)
        target_link_libraries(floatingball-capture-test PRIVATE Qt6::DBus)
    endif()
    if(TARGET LayerShellQt::Interface)
        target_link_libraries(floatingball-capture-test PRIVATE LayerShellQt::Interface)
        target_compile_definitions(floatingball-capture-test PRIVATE FLOATINGBALL_LAYER_SHELL)
    endif()
    if(WIN32)
        target_link_libraries(floatingball-capture-test PRIVATE shell32 ole32 uuid user32 gdi32)
    endif()
    add_test(NAME capture-workflow COMMAND floatingball-capture-test "${CMAKE_CURRENT_BINARY_DIR}/test-data")
    set_tests_properties(capture-workflow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(floatingball-native-capture-check tests/native-capture-check.cpp
        screenshotcapture.cpp screenshotcapture.h screenshotpanel.cpp screenshotpanel.h
        screenshotstore.cpp screenshotstore.h screenrecorder.cpp screenrecorder.h
        floatingwindow.cpp floatingwindow.h)
    target_include_directories(floatingball-native-capture-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(floatingball-native-capture-check PRIVATE Qt6::Widgets Qt6::Multimedia)
    if(UNIX AND NOT APPLE)
        target_link_libraries(floatingball-native-capture-check PRIVATE Qt6::DBus)
    endif()
    if(TARGET LayerShellQt::Interface)
        target_link_libraries(floatingball-native-capture-check PRIVATE LayerShellQt::Interface)
        target_compile_definitions(floatingball-native-capture-check PRIVATE FLOATINGBALL_LAYER_SHELL)
    endif()
    if(WIN32)
        target_link_libraries(floatingball-native-capture-check PRIVATE shell32 ole32 uuid user32 gdi32)
    endif()
endif()
