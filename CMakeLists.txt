cmake_minimum_required(VERSION 3.21)
project(BlockNudge VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static CRT (/MT), so the DLL needs nothing redistributed alongside it.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

# The pure logic: builds on any host, which is what lets the tests run without Windows.
add_library(blocknudge_core STATIC src/nudge.cpp src/scan.cpp src/keys.cpp)
target_include_directories(blocknudge_core PUBLIC src)

if(WIN32)
    include(FetchContent)
    # MinHook v1.3.4, pinned by commit. Its own CMake project is skipped (SOURCE_SUBDIR names
    # a directory that does not exist); the four sources are compiled here instead.
    FetchContent_Declare(minhook
        GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
        GIT_TAG c3fcafdc10146beb5919319d0683e44e3c30d537
        SOURCE_SUBDIR not-used)
    FetchContent_MakeAvailable(minhook)
    add_library(minhook STATIC
        ${minhook_SOURCE_DIR}/src/buffer.c
        ${minhook_SOURCE_DIR}/src/hook.c
        ${minhook_SOURCE_DIR}/src/trampoline.c
        ${minhook_SOURCE_DIR}/src/hde/hde64.c)
    target_include_directories(minhook PUBLIC ${minhook_SOURCE_DIR}/include)
    if(MSVC)
        target_compile_options(minhook PRIVATE /W0)
    else()
        target_compile_options(minhook PRIVATE -w)
    endif()

    add_library(dinput8 SHARED
        src/proxy.cpp src/hook.cpp src/input.cpp src/config.cpp src/log.cpp src/dinput8.def)
    set_target_properties(dinput8 PROPERTIES PREFIX "" OUTPUT_NAME dinput8)
    target_link_libraries(dinput8 PRIVATE blocknudge_core minhook)
    target_compile_definitions(dinput8 PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
        BLOCKNUDGE_VERSION="${PROJECT_VERSION}")
    if(MINGW)
        # Printf from MinGW itself, so formats behave the same whichever C runtime is the default.
        target_compile_definitions(dinput8 PRIVATE __USE_MINGW_ANSI_STDIO=1)
        target_link_options(dinput8 PRIVATE -static -Wl,--no-insert-timestamp
            $<$<NOT:$<CONFIG:Debug>>:-s>)
    endif()

    # Release zip: the zip root is the game folder.
    set(stage ${CMAKE_BINARY_DIR}/package)
    set(zip ${CMAKE_BINARY_DIR}/BlockNudge-${PROJECT_VERSION}.zip)
    add_custom_target(package_zip
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${stage} ${zip}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${stage}
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:dinput8>
            ${CMAKE_SOURCE_DIR}/BlockNudge.ini ${CMAKE_SOURCE_DIR}/README.md
            ${CMAKE_SOURCE_DIR}/LICENSE ${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.md ${stage}
        COMMAND ${CMAKE_COMMAND} -E chdir ${stage} ${CMAKE_COMMAND} -E tar cf ${zip} --format=zip
            dinput8.dll BlockNudge.ini README.md LICENSE THIRD-PARTY-NOTICES.md
        # dist/ is committed: install.cmd copies the DLL from there on the test machine.
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:dinput8> ${CMAKE_SOURCE_DIR}/dist/dinput8.dll
        DEPENDS dinput8
        COMMENT "Packaging ${zip}")
endif()

# Tests run on the build machine, so not when cross-compiling.
if(NOT CMAKE_CROSSCOMPILING)
    enable_testing()
    add_executable(blocknudge_tests tests/test_main.cpp tests/test_nudge.cpp tests/test_scan.cpp)
    target_link_libraries(blocknudge_tests PRIVATE blocknudge_core)
    add_test(NAME blocknudge_tests COMMAND blocknudge_tests)
endif()
