cmake_minimum_required(VERSION 3.20)
project(icky VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Zydis (encrypted field recovery + offline decrypt_scan)
set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_DOXYGEN OFF CACHE BOOL "" FORCE)
set(ZYAN_ZYCORE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../Testing/Icky/deps/zydis/dependencies/zycore" CACHE PATH "" FORCE)
set(_zydis_root "${CMAKE_CURRENT_SOURCE_DIR}/../Testing/Icky/deps/zydis")
if(NOT EXISTS "${_zydis_root}/CMakeLists.txt")
    set(_zydis_root "C:/Users/alfie/3D Objects/Testing/Icky/deps/zydis")
endif()
set(ICKY_HAS_ZYDIS_LIB OFF)
if(EXISTS "${_zydis_root}/CMakeLists.txt")
    add_subdirectory("${_zydis_root}" "${CMAKE_BINARY_DIR}/_zydis" EXCLUDE_FROM_ALL)
    set(ICKY_HAS_ZYDIS_LIB ON)
else()
    message(WARNING "Zydis not found — encrypted field decrypt recovery disabled")
endif()

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
    src/engines/il2cpp/memory_meta.cpp
    src/engines/il2cpp/runtime_dump.cpp
    src/engines/il2cpp/deobf.cpp
    src/engines/il2cpp/decrypt_recover.cpp
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
if(ICKY_HAS_ZYDIS_LIB)
    target_link_libraries(icky PRIVATE Zydis)
    target_compile_definitions(icky PRIVATE ICKY_HAS_ZYDIS=1)
endif()

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

# Offline Zydis GameAssembly decrypt hunter (no inject / no IDA)
option(ICKY_BUILD_DECRYPT_SCAN "Build decrypt_scan Zydis tool" ON)
if(ICKY_BUILD_DECRYPT_SCAN AND ICKY_HAS_ZYDIS_LIB)
    add_executable(decrypt_scan tools/decrypt_scan/main.cpp)
    target_link_libraries(decrypt_scan PRIVATE Zydis)
    target_compile_definitions(decrypt_scan PRIVATE _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(decrypt_scan PRIVATE /W3 /EHsc)
    endif()
    set_target_properties(decrypt_scan PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/Debug"
    )
endif()

