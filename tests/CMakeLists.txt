# lmp_add_test(<name> SOURCES <files...> [LIBS <targets...>])
# Builds one Qt Test executable and registers it with CTest.
function(lmp_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    qt_add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBS})
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    lmp_enable_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

lmp_add_test(tst_app_identity
    SOURCES core/tst_app_identity.cpp
    LIBS lmp_core)

lmp_add_test(tst_private_config
    SOURCES core/tst_private_config.cpp
    LIBS lmp_core)

lmp_add_test(tst_theme
    SOURCES ui/tst_theme.cpp
    LIBS lmp_ui)

lmp_add_test(tst_motion
    SOURCES ui/tst_motion.cpp
    LIBS lmp_ui)
