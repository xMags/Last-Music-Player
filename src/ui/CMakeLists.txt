# The single QML module of the application. C++ types registered for QML and
# every QML file belong to this target.
qt_add_library(lmp_ui STATIC)

# Bundled fonts live in Assets/ with the other shipped files. The alias gives
# them a short path inside the module's resources (FontLoader reads it).
set_source_files_properties("${CMAKE_SOURCE_DIR}/Assets/fonts/MrDeHaviland-Regular.ttf"
    PROPERTIES QT_RESOURCE_ALIAS "fonts/MrDeHaviland-Regular.ttf")
set_source_files_properties("${CMAKE_SOURCE_DIR}/Assets/fonts/ArchivoExpanded-Black.ttf"
    PROPERTIES QT_RESOURCE_ALIAS "fonts/ArchivoExpanded-Black.ttf")
set_source_files_properties("${CMAKE_SOURCE_DIR}/Assets/fonts/JetBrainsMono-Medium.ttf"
    PROPERTIES QT_RESOURCE_ALIAS "fonts/JetBrainsMono-Medium.ttf")
# The design gallery's sample cover: the application logo.
set_source_files_properties("${CMAKE_SOURCE_DIR}/Assets/Square44x44Logo.targetsize-256.png"
    PROPERTIES QT_RESOURCE_ALIAS "dev/sample-cover.png")
# The stripes over a genre tile, from the current app.
set_source_files_properties("${CMAKE_SOURCE_DIR}/Assets/LibraryStripeOverlay.svg"
    PROPERTIES QT_RESOURCE_ALIAS "library/LibraryStripeOverlay.svg")

# Every QML file of the module. File names are unique: each is a type name.
set(LMP_UI_QML_FILES
    qml/Main.qml
    qml/cards/Card.qml
    qml/cards/CountPill.qml
    qml/cards/CardGrid.qml
    qml/cards/EmptyState.qml
    qml/cards/ErrorState.qml
    qml/cards/LinkButton.qml
    qml/cards/SectionHeader.qml
    qml/cards/Shelf.qml
    qml/cards/ShelfArrow.qml
    qml/controls/AccentSwatch.qml
    qml/controls/ActionMenu.qml
    qml/controls/ActionMenuItem.qml
    qml/controls/AppButton.qml
    qml/controls/AppScrollBar.qml
    qml/controls/AppSelect.qml
    qml/controls/AppSlider.qml
    qml/controls/AppSwitch.qml
    qml/controls/ArtTile.qml
    qml/controls/BoxShadow.qml
    qml/controls/ButtonSurface.qml
    qml/controls/Chip.qml
    qml/controls/CompactEmptyState.qml
    qml/controls/ConfirmDialog.qml
    qml/controls/FocusRing.qml
    qml/controls/Icon.qml
    qml/controls/IconButton.qml
    qml/controls/IconShapePath.qml
    qml/controls/PlayButton.qml
    qml/controls/PromptTextField.qml
    qml/controls/Ripple.qml
    qml/controls/SearchField.qml
    qml/controls/SegmentedControl.qml
    qml/controls/Skeleton.qml
    qml/controls/SkeletonGrid.qml
    qml/controls/SkeletonRows.qml
    qml/controls/SkeletonSweep.qml
    qml/controls/SliderBubble.qml
    qml/controls/TextPromptDialog.qml
    qml/controls/Toast.qml
    qml/controls/ToastStack.qml
    qml/dev/Gallery.qml
    qml/dev/GalleryArtwork.qml
    qml/dev/GalleryControls.qml
    qml/dev/GalleryIcons.qml
    qml/dev/GalleryLists.qml
    qml/dev/GalleryOverlays.qml
    qml/dev/GalleryPanels.qml
    qml/dev/GallerySection.qml
    qml/dev/GalleryTokens.qml
    qml/home/HeroField.qml
    qml/home/HomeHero.qml
    qml/home/HomePage.qml
    qml/home/MixCard.qml
    qml/home/QuickTile.qml
    qml/library/ArtistPage.qml
    qml/library/CollectionCard.qml
    qml/library/CollectionCover.qml
    qml/library/CollectionDialogs.qml
    qml/library/CollectionHeader.qml
    qml/library/CollectionPage.qml
    qml/library/GenreMark.qml
    qml/library/GenreTile.qml
    qml/library/LibraryPage.qml
    qml/library/PlayPill.qml
    qml/library/RoundAction.qml
    qml/library/ViewSwitch.qml
    qml/player/FullscreenBackdrop.qml
    qml/player/FullscreenBody.qml
    qml/player/FullscreenButton.qml
    qml/player/FullscreenPlayButton.qml
    qml/player/FullscreenPlayer.qml
    qml/player/FullscreenSidePanel.qml
    qml/player/FullscreenTabs.qml
    qml/player/IdleArt.qml
    qml/player/PlayerBar.qml
    qml/player/PlayerTrackBlock.qml
    qml/player/PlayerTransport.qml
    qml/player/QueueArtGlow.qml
    qml/player/QueueIdlePanel.qml
    qml/player/QueueList.qml
    qml/player/QueueNowPlayingHeader.qml
    qml/player/QueueRail.qml
    qml/player/QueueRow.qml
    qml/player/QueueSectionHeader.qml
    qml/player/RailSectionLabel.qml
    qml/account/SignInDialog.qml
    qml/search/BrowsePage.qml
    qml/search/CategoryTile.qml
    qml/search/SearchSectionHead.qml
    qml/search/SearchTopCard.qml
    qml/settings/AccountBlock.qml
    qml/settings/ApiKeyForm.qml
    qml/settings/SettingsAboutCard.qml
    qml/settings/SettingsAppearanceCard.qml
    qml/settings/SettingsCard.qml
    qml/settings/SettingsDivider.qml
    qml/settings/SettingsLibraryCard.qml
    qml/settings/SettingsPage.qml
    qml/settings/SettingsProfileCard.qml
    qml/settings/SettingsRow.qml
    qml/settings/SettingsSection.qml
    qml/settings/SettingsSectionHead.qml
    qml/settings/SettingsSelectRow.qml
    qml/settings/SettingsSwitchRow.qml
    qml/settings/SourceModeCards.qml
    qml/settings/ThemeTile.qml
    qml/shell/BrandMark.qml
    qml/shell/ContentPanel.qml
    qml/shell/FirstRunPage.qml
    qml/shell/CaptionButton.qml
    qml/shell/NavItem.qml
    qml/shell/PageHost.qml
    qml/shell/PlaceholderPage.qml
    qml/shell/ProfileButton.qml
    qml/shell/RightRail.qml
    qml/shell/ScrollMemory.qml
    qml/shell/Shell.qml
    qml/welcome/WelcomeFlow.qml
    qml/shell/Sidebar.qml
    qml/shell/SidebarPlaylistRow.qml
    qml/shell/TitleStrip.qml
    qml/tracks/EqBars.qml
    qml/tracks/TableHeading.qml
    qml/tracks/TrackMenus.qml
    qml/tracks/TrackListView.qml
    qml/tracks/TrackRow.qml
    qml/tracks/TrackTile.qml)

qt_add_qml_module(lmp_ui
    URI LastMusicPlayer
    VERSION 1.0
    # QML tooling expects the output folder to end with the module path.
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml/LastMusicPlayer"
    SOURCES
        app/AppInfo.cpp
        app/AppInfo.h
        dev/GalleryViewModel.cpp
        dev/GalleryViewModel.h
        dev/ThemeTokenListModel.cpp
        dev/ThemeTokenListModel.h
        icons/IconData.h
        icons/IconDataLine.cpp
        icons/IconDataLogos.cpp
        icons/IconDataSolid.cpp
        icons/IconPart.cpp
        icons/IconPart.h
        icons/IconSet.cpp
        icons/IconSet.h
        home/HomeListModels.cpp
        home/HomeListModels.h
        home/HomeViewModel.cpp
        home/HomeViewModel.h
        library/AccountArtistsViewModel.cpp
        library/AccountArtistsViewModel.h
        library/CollectionCardModel.cpp
        library/CollectionCardModel.h
        library/CollectionEditor.cpp
        library/CollectionEditor.h
        library/CollectionViewModel.cpp
        library/CollectionViewModel.h
        library/GenreArtwork.cpp
        library/GenreArtwork.h
        library/LibraryViewModel.cpp
        library/LibraryViewModel.h
        library/LocalArtistViewModel.cpp
        library/LocalArtistViewModel.h
        library/SystemPlaylists.cpp
        library/SystemPlaylists.h
        menus/MenuItemListModel.cpp
        menus/MenuItemListModel.h
        navigation/Navigator.cpp
        navigation/Navigator.h
        navigation/Route.cpp
        navigation/Route.h
        navigation/RouteRules.cpp
        navigation/RouteRules.h
        player/AmbientAccentSampler.cpp
        player/AmbientAccentSampler.h
        player/CoverSource.cpp
        player/CoverSource.h
        player/EmbeddedCoverImageProvider.cpp
        player/EmbeddedCoverImageProvider.h
        player/FullscreenPlayerViewModel.cpp
        player/FullscreenPlayerViewModel.h
        images/ArtworkImageProvider.cpp
        images/ArtworkImageProvider.h
        player/PlaybackNoticeToasts.cpp
        player/PlaybackNoticeToasts.h
        player/PlayerBarViewModel.cpp
        player/PlayerBarViewModel.h
        player/QueueListDiff.cpp
        player/QueueListDiff.h
        player/QueueListModel.cpp
        player/QueueListModel.h
        player/QueueRailViewModel.cpp
        player/QueueRailViewModel.h
        player/QueueRowDrag.cpp
        player/QueueRowDrag.h
        search/SearchSource.cpp
        search/SearchSource.h
        search/SearchViewModel.cpp
        search/SearchViewModel.h
        shell/ConnectivityViewModel.cpp
        shell/ConnectivityViewModel.h
        welcome/WelcomeViewModel.cpp
        welcome/WelcomeViewModel.h
        settings/DataSettingsViewModel.cpp
        settings/DataSettingsViewModel.h
        settings/FirstRunViewModel.cpp
        settings/FirstRunViewModel.h
        settings/LibrarySettingsViewModel.cpp
        settings/LibrarySettingsViewModel.h
        settings/SettingsAboutViewModel.cpp
        settings/SettingsAboutViewModel.h
        settings/SettingsViewModel.cpp
        settings/SettingsViewModel.h
        settings/SourceModeViewModel.cpp
        settings/SourceModeViewModel.h
        shell/AppRoot.cpp
        shell/AppRoot.h
        shell/PlaybackKeyGuard.cpp
        shell/PlaybackKeyGuard.h
        shell/ProfileViewModel.cpp
        shell/ProfileViewModel.h
        shell/ShellLayoutViewModel.cpp
        shell/ShellLayoutViewModel.h
        shell/SidebarListModels.cpp
        shell/SidebarListModels.h
        shell/SidebarViewModel.cpp
        shell/SidebarViewModel.h
        shell/WindowChromeViewModel.cpp
        shell/WindowChromeViewModel.h
        theme/ColorMix.cpp
        theme/ColorMix.h
        theme/FontLoader.cpp
        theme/FontLoader.h
        theme/Motion.cpp
        theme/Motion.h
        theme/ShadowLayer.cpp
        theme/ShadowLayer.h
        theme/TextStyles.cpp
        theme/TextStyles.h
        theme/Theme.cpp
        theme/Theme.h
        theme/ThemeColors.cpp
        theme/ThemeColors.h
        theme/ThemeConstants.cpp
        theme/ThemeConstants.h
        theme/ThemeFullscreen.cpp
        theme/ThemeFullscreen.h
        theme/ThemePalette.cpp
        theme/ThemePalette.h
        theme/ThemeSettingsBinding.cpp
        theme/ThemeSettingsBinding.h
        theme/ThemeTypography.cpp
        theme/ThemeTypography.h
        toasts/ToastQueue.cpp
        toasts/ToastQueue.h
        tracks/TrackListModel.cpp
        tracks/TrackListModel.h
        tracks/TrackMenuBuilder.cpp
        tracks/TrackMenuBuilder.h
        tracks/TrackMenuController.cpp
        tracks/TrackMenuController.h
        tracks/QueryTrackSource.cpp
        tracks/QueryTrackSource.h
    QML_FILES ${LMP_UI_QML_FILES}
    RESOURCES
        "${CMAKE_SOURCE_DIR}/Assets/fonts/MrDeHaviland-Regular.ttf"
        "${CMAKE_SOURCE_DIR}/Assets/fonts/ArchivoExpanded-Black.ttf"
        "${CMAKE_SOURCE_DIR}/Assets/fonts/JetBrainsMono-Medium.ttf"
        "${CMAKE_SOURCE_DIR}/Assets/Square44x44Logo.targetsize-256.png"
        "${CMAKE_SOURCE_DIR}/Assets/LibraryStripeOverlay.svg")

# Every folder that holds a header with a QML-registered type. The generated
# registration code includes those headers by file name only, so each folder
# must be on the include path and header file names must be unique in src/ui.
set(LMP_UI_TYPE_FOLDERS
    app
    dev
    home
    icons
    library
    menus
    search
    navigation
    player
    settings
    shell
    theme
    toasts
    tracks)

target_include_directories(lmp_ui
    PUBLIC "${CMAKE_SOURCE_DIR}/src"
    PRIVATE ${LMP_UI_TYPE_FOLDERS})

target_link_libraries(lmp_ui
    PUBLIC lmp_core Qt6::Concurrent Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)

lmp_enable_warnings(lmp_ui)

# The QML compiler turns each QML file into C++ that reads results of C++
# methods through QJSEngine::fromVariant<T>(). For bool, int, double and
# QString that template ends in a branch the compiler can never reach, and
# MSVC reports C4702 inside the Qt header. Silence that one warning in those
# generated files only; hand-written code keeps it.
if(MSVC)
    foreach(qml_file IN LISTS LMP_UI_QML_FILES)
        string(REGEX REPLACE "\\.qml$" "" qml_stem "${qml_file}")
        set_source_files_properties(
            "${CMAKE_CURRENT_BINARY_DIR}/.rcc/qmlcache/lmp_ui_${qml_stem}_qml.cpp"
            TARGET_DIRECTORY lmp_ui
            PROPERTIES COMPILE_OPTIONS /wd4702)
    endforeach()
endif()
