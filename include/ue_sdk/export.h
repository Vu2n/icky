#pragma once

// Cross-platform DLL export / import macros

#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(UE_SDK_BUILDING_DLL)
    #define UE_SDK_API __declspec(dllexport)
  #else
    #define UE_SDK_API __declspec(dllimport)
  #endif
  #define UE_SDK_CALL __cdecl
#else
  #define UE_SDK_API __attribute__((visibility("default")))
  #define UE_SDK_CALL
#endif

#ifdef __cplusplus
  #define UE_SDK_EXTERN_C extern "C"
#else
  #define UE_SDK_EXTERN_C
#endif
