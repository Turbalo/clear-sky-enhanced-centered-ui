cmake_minimum_required(VERSION 3.24)

project(ClearSky4x3UI VERSION 1.0.1 LANGUAGES C CXX ASM_MASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(cs4x3ui SHARED
    src/dllmain.cpp
    src/exports.asm
    src/log.cpp
    src/module.cpp
    src/runtime_hooks.cpp
    src/safe_zone.cpp
    src/signature.cpp
    src/version.rc
    vendor/minhook/src/buffer.c
    vendor/minhook/src/hook.c
    vendor/minhook/src/trampoline.c
    vendor/minhook/src/hde/hde64.c
)

set_source_files_properties(
    vendor/minhook/src/buffer.c
    vendor/minhook/src/hook.c
    vendor/minhook/src/trampoline.c
    vendor/minhook/src/hde/hde64.c
    PROPERTIES COMPILE_OPTIONS "/W0;/WX-"
)

target_include_directories(cs4x3ui PRIVATE
    src
    vendor/minhook/include
    vendor/minhook/src
    vendor/minhook/src/hde
)
target_compile_definitions(cs4x3ui PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)
target_compile_options(cs4x3ui PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:/W4>
    $<$<COMPILE_LANGUAGE:CXX>:/WX>
    $<$<COMPILE_LANGUAGE:CXX>:/permissive->
)
target_link_libraries(cs4x3ui PRIVATE bcrypt version)

set_target_properties(cs4x3ui PROPERTIES
    OUTPUT_NAME "dinput8"
    LINK_FLAGS "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/dinput8.def"
)

enable_testing()

add_executable(safe_zone_tests
    tests/safe_zone_tests.cpp
    src/safe_zone.cpp
)
target_include_directories(safe_zone_tests PRIVATE src)
target_compile_options(safe_zone_tests PRIVATE /W4 /WX /permissive-)
add_test(NAME safe_zone_tests COMMAND safe_zone_tests)

add_executable(proxy_smoke_test tests/proxy_smoke_test.cpp)
target_compile_definitions(proxy_smoke_test PRIVATE
    DIRECTINPUT_VERSION=0x0800
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)
target_compile_options(proxy_smoke_test PRIVATE /W4 /WX /permissive-)
target_link_libraries(proxy_smoke_test PRIVATE dxguid)
add_test(
    NAME proxy_smoke_test
    COMMAND proxy_smoke_test $<TARGET_FILE:cs4x3ui>
)
