qt_add_library(lmp_core STATIC
    app/AppIdentity.cpp
    app/AppIdentity.h
    config/PrivateConfig.cpp
    config/PrivateConfig.h
    ../platform/IReducedMotion.h)

target_include_directories(lmp_core
    PUBLIC "${CMAKE_SOURCE_DIR}/src"
    PRIVATE "${CMAKE_BINARY_DIR}/generated")

target_link_libraries(lmp_core PUBLIC Qt6::Core Qt6::Network)

target_compile_definitions(lmp_core PRIVATE
    LMP_VERSION_STRING="${PROJECT_VERSION}")

lmp_enable_strict_warnings(lmp_core)
