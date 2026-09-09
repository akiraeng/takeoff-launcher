cmake_minimum_required(VERSION 3.21)
project(Takeoff VERSION 1.0.2 LANGUAGES CXX)

function(add_launcher target)
add_executable(${target} WIN32
    src/main.cpp
    src/launcher.h
    src/file_index.h
    src/search.h
    src/settings.h
    src/updates.h
    src/Takeoff.manifest
    src/Takeoff.rc
)

target_compile_features(${target} PRIVATE cxx_std_17)
target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(${target} PRIVATE
    dwmapi
    shell32
    shlwapi
    user32
    gdi32
    ole32
    d2d1
    dwrite
    windowscodecs
    imm32
    advapi32
    winhttp
)

set_target_properties(${target} PROPERTIES
    VS_GLOBAL_EnableCppCoreCheck false
)
endfunction()

add_launcher(Takeoff)

include(CTest)
if(BUILD_TESTING)
    add_executable(TakeoffCoreTests tests/core_tests.cpp)
    target_compile_features(TakeoffCoreTests PRIVATE cxx_std_17)
    target_compile_options(TakeoffCoreTests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(TakeoffCoreTests PRIVATE winhttp)
    add_test(NAME TakeoffCoreTests COMMAND TakeoffCoreTests)

    # Separate class, mutex, fixture apps, and no global shortcut. Never attaches
    # to or replaces a user's running launcher.
    add_launcher(TakeoffUiTests)
    target_compile_definitions(TakeoffUiTests PRIVATE TAKEOFF_UI_TEST)
endif()
