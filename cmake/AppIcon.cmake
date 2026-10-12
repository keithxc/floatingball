# Keep a portable alpha icon, plus Apple's native layered icon on recent Xcode.
target_sources(FloatingBall PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons.qrc")

if(WIN32)
    enable_language(RC)
    configure_file(packaging/floatingball.rc.in "${CMAKE_CURRENT_BINARY_DIR}/floatingball.rc" @ONLY)
    target_sources(FloatingBall PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/floatingball.rc")
elseif(APPLE)
    set(FLOATINGBALL_MAC_ICON_PLIST_ENTRY "")
    target_sources(FloatingBall PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/assets/AppIcon.icns")
    set_source_files_properties(assets/AppIcon.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(FloatingBall PROPERTIES
        MACOSX_BUNDLE_ICON_FILE AppIcon.icns
        MACOSX_BUNDLE_BUNDLE_NAME FloatingBall
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in")
    option(FLOATINGBALL_NATIVE_APP_ICON "Compile Apple dark/clear icon variants with Xcode 26+" ON)
    if(FLOATINGBALL_NATIVE_APP_ICON)
        set(_actool_hints /Applications/Xcode.app/Contents/Developer/usr/bin)
        if(NOT "$ENV{DEVELOPER_DIR}" STREQUAL "")
            list(PREPEND _actool_hints "$ENV{DEVELOPER_DIR}/usr/bin")
        endif()
        # /usr/bin/actool is only an xcrun shim and can select Command Line Tools.
        find_program(FLOATINGBALL_ACTOOL actool HINTS ${_actool_hints} NO_DEFAULT_PATH)
        if(FLOATINGBALL_ACTOOL)
            get_filename_component(_actool_bin "${FLOATINGBALL_ACTOOL}" DIRECTORY)
            get_filename_component(_actool_usr "${_actool_bin}" DIRECTORY)
            get_filename_component(_developer_dir "${_actool_usr}" DIRECTORY)
            execute_process(COMMAND "${CMAKE_COMMAND}" -E env "DEVELOPER_DIR=${_developer_dir}"
                "${FLOATINGBALL_ACTOOL}" --version
                OUTPUT_VARIABLE _actool_version RESULT_VARIABLE _actool_status)
            if(_actool_status EQUAL 0 AND _actool_version MATCHES "short-bundle-version</key>[ \n\r\t]*<string>([0-9]+)")
                set(_actool_major "${CMAKE_MATCH_1}")
            else()
                set(_actool_major 0)
            endif()
            if(_actool_major LESS 26)
                message(STATUS "Xcode 26+ required for dark/clear icons: using the transparent ICNS fallback")
                return()
            endif()
            set(FLOATINGBALL_MAC_ICON_PLIST_ENTRY "<key>CFBundleIconName</key><string>AppIcon</string>")
            set(_icon_output "${CMAKE_CURRENT_BINARY_DIR}/app-icon")
            file(MAKE_DIRECTORY "${_icon_output}/tmp")
            set(_icon_min_os "${CMAKE_OSX_DEPLOYMENT_TARGET}")
            if(NOT _icon_min_os)
                set(_icon_min_os 12.0)
            endif()
            file(GLOB _icon_layers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/AppIcon.icon/Assets/*.svg")
            add_custom_command(OUTPUT "${_icon_output}/Assets.car"
                COMMAND "${CMAKE_COMMAND}" -E env
                    "DEVELOPER_DIR=${_developer_dir}" "TMPDIR=${_icon_output}/tmp"
                    "${FLOATINGBALL_ACTOOL}" --platform macosx --target-device mac
                    --minimum-deployment-target "${_icon_min_os}" --app-icon AppIcon
                    --output-partial-info-plist "${_icon_output}/partial.plist"
                    --compile "${_icon_output}" "${CMAKE_CURRENT_SOURCE_DIR}/assets/AppIcon.icon"
                DEPENDS assets/AppIcon.icon/icon.json ${_icon_layers}
                COMMENT "Compiling Apple layered app icon (default, dark, clear)"
                VERBATIM)
            add_custom_target(floatingball-app-icon DEPENDS "${_icon_output}/Assets.car")
            add_dependencies(FloatingBall floatingball-app-icon)
            target_sources(FloatingBall PRIVATE "${_icon_output}/Assets.car")
            set_source_files_properties("${_icon_output}/Assets.car" PROPERTIES
                GENERATED TRUE MACOSX_PACKAGE_LOCATION Resources)
        else()
            message(STATUS "Xcode actool not found: using the transparent ICNS fallback")
        endif()
    endif()
endif()

if(UNIX AND NOT APPLE)
    install(FILES packaging/floatingball.desktop DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    install(FILES assets/floatingball.svg DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
    foreach(size 16 32 48 64 128 256 512)
        install(FILES "assets/floatingball-${size}.png"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps"
            RENAME floatingball.png)
    endforeach()
endif()
