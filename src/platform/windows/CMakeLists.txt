# Windows implementations of the interfaces in src/platform. Only the
# application target links this library.
qt_add_library(lmp_platform_windows STATIC
    WindowsAppIdentity.cpp
    WindowsAppIdentity.h
    WindowsReducedMotion.cpp
    WindowsReducedMotion.h
    WindowsSingleInstance.cpp
    WindowsSingleInstance.h)

target_link_libraries(lmp_platform_windows
    PUBLIC lmp_core Qt6::Core
    PRIVATE user32 shell32)

target_compile_definitions(lmp_platform_windows PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE)

lmp_enable_strict_warnings(lmp_platform_windows)
