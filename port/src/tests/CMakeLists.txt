# darkcloud_tests links the whole port and game so a test can exercise a
# replacement unit against the real game types. Each *_test.cpp holds
# GoogleTest cases; ctest runs one process per case, since the game's state is
# global.
include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.18.0
    GIT_SHALLOW TRUE
    FIND_PACKAGE_ARGS NAMES GTest)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)
add_executable(darkcloud_tests ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ${TEST_SOURCES})
set_target_properties(darkcloud_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_include_directories(darkcloud_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
dc_link_game(darkcloud_tests)
kz_port_tests(darkcloud_tests)
target_link_libraries(darkcloud_tests PRIVATE GTest::gtest nlohmann_json::nlohmann_json)
add_library(chronicle_native_fixture SHARED mod_native_fixture.c)
target_include_directories(chronicle_native_fixture PRIVATE ${PORT_DIR})
set_target_properties(chronicle_native_fixture PROPERTIES C_STANDARD 99)
add_dependencies(darkcloud_tests chronicle_native_fixture)
target_compile_definitions(darkcloud_tests PRIVATE CHRONICLE_NATIVE_FIXTURE="$<TARGET_FILE:chronicle_native_fixture>")

foreach(variant v2 v2_bad v2_no_events v2_menu v2_menu_bad)
    add_library(chronicle_native_${variant}_fixture SHARED mod_native_v2_fixture.c)
    target_include_directories(chronicle_native_${variant}_fixture PRIVATE ${PORT_DIR})
    set_target_properties(chronicle_native_${variant}_fixture PROPERTIES C_STANDARD 99)
    add_dependencies(darkcloud_tests chronicle_native_${variant}_fixture)
endforeach()
target_compile_definitions(chronicle_native_v2_bad_fixture PRIVATE FIXTURE_ABI=99)
target_compile_definitions(chronicle_native_v2_no_events_fixture PRIVATE FIXTURE_EVENTS=0)
target_compile_definitions(chronicle_native_v2_menu_fixture PRIVATE FIXTURE_MENU=1)
target_compile_definitions(chronicle_native_v2_menu_bad_fixture PRIVATE FIXTURE_MENU=1 FIXTURE_MENU_BAD=1)
target_compile_definitions(darkcloud_tests PRIVATE
    CHRONICLE_NATIVE_V2_FIXTURE="$<TARGET_FILE:chronicle_native_v2_fixture>"
    CHRONICLE_NATIVE_V2_BAD_FIXTURE="$<TARGET_FILE:chronicle_native_v2_bad_fixture>"
    CHRONICLE_NATIVE_V2_NO_EVENTS_FIXTURE="$<TARGET_FILE:chronicle_native_v2_no_events_fixture>"
    CHRONICLE_NATIVE_V2_MENU_FIXTURE="$<TARGET_FILE:chronicle_native_v2_menu_fixture>"
    CHRONICLE_NATIVE_V2_MENU_BAD_FIXTURE="$<TARGET_FILE:chronicle_native_v2_menu_bad_fixture>")

# A case is a unit test: it finishes in milliseconds, and one that does not is cut off. The clock
# cases measure short sleeps, which the parallel gfx cases skew, so they run on their own.
set(DC_TEST_TIMEOUT 5 CACHE STRING "Seconds a test case may run before it is cut off")
gtest_discover_tests(darkcloud_tests
    DISCOVERY_MODE PRE_TEST
    TEST_FILTER -PlatformClock.*
    PROPERTIES TIMEOUT ${DC_TEST_TIMEOUT} ENVIRONMENT "DC_TEST_FONT=${CMAKE_SOURCE_DIR}/tools/font/DarkCloudCompendium.ttf")
gtest_discover_tests(darkcloud_tests
    DISCOVERY_MODE PRE_TEST
    TEST_FILTER PlatformClock.*
    PROPERTIES TIMEOUT ${DC_TEST_TIMEOUT} ENVIRONMENT "DC_TEST_FONT=${CMAKE_SOURCE_DIR}/tools/font/DarkCloudCompendium.ttf" RUN_SERIAL TRUE)
