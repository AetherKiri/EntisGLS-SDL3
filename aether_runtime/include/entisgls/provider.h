#pragma once
#include "engine_runtime_provider.h"

#if defined(_WIN32)
# if defined(AETHER_ENTISGLS_EXPORTS)
#  define AETHER_ENTISGLS_API __declspec(dllexport)
# else
#  define AETHER_ENTISGLS_API __declspec(dllimport)
# endif
#else
# define AETHER_ENTISGLS_API __attribute__((visibility("default")))
#endif

// One exported C symbol keeps the legacy SDK, TJS and SDL globals isolated
// from the other engines in the host. The descriptor lives for the process.
extern "C" AETHER_ENTISGLS_API const engine_runtime_provider_v1_t* AetherEntisGLSProvider();
