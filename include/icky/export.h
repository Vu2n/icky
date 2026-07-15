#pragma once

#if defined(_WIN32)
  #if defined(ICKY_BUILDING_DLL)
    #define ICKY_API __declspec(dllexport)
  #else
    #define ICKY_API __declspec(dllimport)
  #endif
  #define ICKY_CALL __cdecl
#else
  #define ICKY_API __attribute__((visibility("default")))
  #define ICKY_CALL
#endif

#ifdef __cplusplus
  #define ICKY_EXTERN_C extern "C"
#else
  #define ICKY_EXTERN_C
#endif
