qt_add_executable(lmp_app WIN32
    CloseToTrayController.cpp
    CloseToTrayController.h
    CompositionRoot.cpp
    CompositionRoot.h
    WindowPlacementKeeper.cpp
    WindowPlacementKeeper.h
    main.cpp
    AppIcon.rc
    app.manifest)

# Installed copies and their shortcuts refer to this exact file name.
set_target_properties(lmp_app PROPERTIES
    OUTPUT_NAME "Last_Music_Player"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

target_link_libraries(lmp_app
    PRIVATE lmp_core lmp_platform_windows lmp_ui lmp_uiplugin Qt6::Gui Qt6::Qml Qt6::Quick)

lmp_enable_warnings(lmp_app)
