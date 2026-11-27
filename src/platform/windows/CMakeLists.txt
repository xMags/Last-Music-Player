# Windows implementations of the interfaces in src/platform. Only the
# application target links this library.
qt_add_library(lmp_platform_windows STATIC
    WindowsAppIdentity.cpp
    WindowsAppIdentity.h
    WindowsAudioOutputs.cpp
    WindowsDnsServiceBrowser.cpp
    WindowsDnsServiceBrowser.h
    WindowsAudioOutputs.h
    WindowsAudioPlayer.cpp
    WindowsAudioPlayer.h
    WindowsAudioPlayerDevice.cpp
    WindowsAudioPlayerEvents.cpp
    WindowsAudioPlayerNative.h
    WindowsCredentialStore.cpp
    WindowsCredentialStore.h
    WindowsExternalLinks.cpp
    WindowsExternalLinks.h
    WindowsFolderPicker.cpp
    WindowsFolderPicker.h
    WindowsMediaControls.cpp
    WindowsMediaControls.h
    WindowsNetworkStatus.cpp
    WindowsNetworkStatus.h
    WindowsPowerStatus.cpp
    WindowsPowerStatus.h
    WindowsReducedMotion.cpp
    WindowsReducedMotion.h
    WindowsActivationRequests.cpp
    WindowsActivationRequests.h
    WindowsSingleInstance.cpp
    WindowsSingleInstance.h
    WindowsTagReader.cpp
    WindowsTray.cpp
    WindowsTray.h
    WindowsTagReader.h
    WindowsWindowChrome.cpp
    WindowsWindowChrome.h
    WindowsWindowPlacement.cpp
    WindowsWindowPlacement.h)

target_link_libraries(lmp_platform_windows
    PUBLIC lmp_core Qt6::Core
    PRIVATE comctl32 user32 shell32 ole32 dwmapi advapi32 dnsapi windowsapp)

target_compile_definitions(lmp_platform_windows PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE)

# The Windows Runtime projection headers ship with the Windows SDK, whose
# include folders the MSVC environment already lists. They need standard
# coroutine support, which C++20 provides, and nothing else.
lmp_enable_strict_warnings(lmp_platform_windows)
