#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(IPGEOLOCATION_SHARED)
#    if defined(IPGEOLOCATION_BUILDING_LIBRARY)
#      define IPGEOLOCATION_API __declspec(dllexport)
#    else
#      define IPGEOLOCATION_API __declspec(dllimport)
#    endif
#  else
#    define IPGEOLOCATION_API
#  endif
#  define IPGEOLOCATION_HIDDEN
#elif defined(__GNUC__) || defined(__clang__)
#  define IPGEOLOCATION_API __attribute__((visibility("default")))
#  define IPGEOLOCATION_HIDDEN __attribute__((visibility("hidden")))
#else
#  define IPGEOLOCATION_API
#  define IPGEOLOCATION_HIDDEN
#endif
