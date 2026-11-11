#pragma once

// Architecture detection macros
#if defined(__x86_64__) || defined(_M_X64)
    #define ARCH_X86_64 1
#elif defined(__arm64__) || defined(__aarch64__) || defined(_M_ARM64)
    #define ARCH_ARM64 1
#else
    #error "Unsupported architecture"
#endif

// Compiler detection
#if defined(__clang__)
    #define COMPILER_CLANG 1
#elif defined(__GNUC__)
    #define COMPILER_GCC 1
#elif defined(_MSC_VER)
    #define COMPILER_MSVC 1
#endif

// Logging and debugging macro
#ifdef ARCH_ARM64
    #define ARM64_CONTEXT_DEBUG(x) printf("ARM64 Context Debug: %s\n", x)
#else
    #define ARM64_CONTEXT_DEBUG(x)
#endif

// Conditional threading model
#ifdef ARCH_ARM64
    #define USE_STD_THREAD 1
#else
    #define USE_CONTEXT_SWITCH 1
#endif

