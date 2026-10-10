cmake_minimum_required(VERSION 3.5)

project(FloatingBall VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets LinguistTools)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets LinguistTools)

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
        floatingball.h
        floatingball.ui
        ${TS_FILES}
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(FloatingBall
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET FloatingBall APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation

    qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
else()
    if(ANDROID)
        add_library(FloatingBall SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(FloatingBall
            ${PROJECT_SOURCES}
        )
    endif()

    qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
endif()

target_link_libraries(FloatingBall PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
if(UNIX AND NOT APPLE AND QT_VERSION_MAJOR EQUAL 6)
    find_package(LayerShellQt 6.6 QUIET)
    if(TARGET LayerShellQt::Interface)
        target_link_libraries(FloatingBall PRIVATE LayerShellQt::Interface)
        target_compile_definitions(FloatingBall PRIVATE FLOATINGBALL_LAYER_SHELL)
    endif()
endif()

if(WIN32)
    target_link_libraries(FloatingBall PRIVATE shell32 ole32 uuid user32)
endif()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.FloatingBall)
endif()
set_target_properties(FloatingBall PROPERTIES
    ${BUNDLE_ID_OPTION}
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

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(FloatingBall)
endif()

option(FLOATINGBALL_BUILD_TESTS "Build screenshot and action integration checks" OFF)
if(FLOATINGBALL_BUILD_TESTS)
    enable_testing()
    add_executable(floatingball-screenshot-test
        tests/screenshot-test.cpp floatingball.cpp floatingball.h floatingball.ui
        screenshotpanel.cpp screenshotpanel.h screenshotstore.cpp screenshotstore.h)
    target_sources(floatingball-screenshot-test PRIVATE floatingwindow.cpp floatingwindow.h)
    target_include_directories(floatingball-screenshot-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(floatingball-screenshot-test PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
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
        target_link_libraries(floatingball-screenshot-test PRIVATE shell32 ole32 uuid user32)
    endif()
    add_test(NAME screenshot-integration COMMAND floatingball-screenshot-test "${CMAKE_CURRENT_BINARY_DIR}/test-data")
    set_tests_properties(screenshot-integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
