cmake_minimum_required(VERSION 3.20)
project(icky VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(icky SHARED
    src/dllmain.cpp
    src/console/menu.cpp
    src/core/logger.cpp
    src/core/memory.cpp
    src/core/modules.cpp
    src/core/pattern.cpp
    src/core/fsutil.cpp
    src/core/pe.cpp
    src/engine/registry.cpp
    src/engines/unreal/unreal_engine.cpp
    src/engines/unreal/ue_find.cpp
    src/engines/unreal/ue_names.cpp
    src/engines/unreal/ue_dump.cpp
    src/engines/unreal/ue_fname_call.cpp
    src/engines/il2cpp/il2cpp_engine.cpp
    src/engines/il2cpp/metadata.cpp
    src/engines/il2cpp/string_decrypt.cpp
    src/engines/mono/mono_engine.cpp
    src/engines/mono/string_decrypt.cpp
    src/engines/source/source_engine.cpp
    src/generate/sdk_writer.cpp
    src/generate/dump_format.cpp
)

target_include_directories(icky
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(icky PRIVATE
    ICKY_BUILDING_DLL=1
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(icky PRIVATE psapi version)

if(MSVC)
    target_compile_options(icky PRIVATE /W3 /permissive- /Zc:__cplusplus /EHsc)
    # Avoid DllMain CRT issues with static init in thread
    target_link_options(icky PRIVATE /INCREMENTAL:NO)
else()
    target_compile_options(icky PRIVATE -Wall -Wextra)
endif()

set_target_properties(icky PROPERTIES
    OUTPUT_NAME "icky"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/Debug"
)

# Optional: tiny external loader for testing without a real injector
option(ICKY_BUILD_LOADER "Build test injector loader" ON)
if(ICKY_BUILD_LOADER)
    add_executable(icky_loader examples/loader.cpp)
    if(MSVC)
        target_compile_options(icky_loader PRIVATE /W3)
        target_compile_definitions(icky_loader PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endif()

install(TARGETS icky RUNTIME DESTINATION bin LIBRARY DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
