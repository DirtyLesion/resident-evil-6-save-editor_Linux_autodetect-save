cmake_minimum_required(VERSION 3.20)
project(RE6SaveEditor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
find_package(Qt6 6.3 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

# Personal savedata.bin files are private user data and must not be committed.
# Set this to a local fixture path to enable the additional real-save regression.
set(RE6_SAVE_FIXTURE "" CACHE FILEPATH "Optional local Resident Evil 6 savedata.bin for regression testing")

add_executable(RE6SaveEditor
    src/main_linux.cpp
    src/save_linux.cpp
    src/fields.cpp
    src/steam_save_discovery.cpp
)
target_include_directories(RE6SaveEditor PRIVATE src)
target_link_libraries(RE6SaveEditor PRIVATE Qt6::Widgets)
target_compile_options(RE6SaveEditor PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    add_executable(RE6SaveFileTests
        tests/save_linux_test.cpp
        src/save_linux.cpp
        src/fields.cpp
    )
    target_include_directories(RE6SaveFileTests PRIVATE src)
    target_compile_options(RE6SaveFileTests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME linux-save-roundtrip COMMAND RE6SaveFileTests)

    add_executable(RE6SteamSaveDiscoveryTests
        tests/steam_save_discovery_test.cpp
        src/steam_save_discovery.cpp
    )
    target_include_directories(RE6SteamSaveDiscoveryTests PRIVATE src)
    target_compile_options(RE6SteamSaveDiscoveryTests PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(RE6SteamSaveDiscoveryTests PRIVATE Qt6::Core)
    add_test(NAME steam-save-auto-detect COMMAND RE6SteamSaveDiscoveryTests)

    if(RE6_SAVE_FIXTURE)
        if(NOT EXISTS "${RE6_SAVE_FIXTURE}")
            message(FATAL_ERROR "RE6_SAVE_FIXTURE was set but the file does not exist: ${RE6_SAVE_FIXTURE}")
        endif()
        add_executable(RE6RealSaveFileTests
            tests/real_save_test.cpp
            src/save_linux.cpp
            src/fields.cpp
        )
        target_include_directories(RE6RealSaveFileTests PRIVATE src)
        target_compile_options(RE6RealSaveFileTests PRIVATE -Wall -Wextra -Wpedantic)
        add_test(
            NAME linux-real-save-regression
            COMMAND RE6RealSaveFileTests "${RE6_SAVE_FIXTURE}"
        )
    endif()
endif()

install(TARGETS RE6SaveEditor RUNTIME DESTINATION bin)
install(FILES packaging/re6-save-editor.desktop DESTINATION share/applications)
install(FILES packaging/re6-save-editor.svg DESTINATION share/icons/hicolor/scalable/apps)
