#pragma once

// ESSENTIAL HEADERS -------------------------------------------
// std::uintx_t
#include <cstdint>
// std::size_t
#include <cstddef>
// std::abort, std::malloc, std::calloc, etc.
#include <cstdlib>


#define UINT8_COUNT UINT8_MAX + 1

/*
    Set this on if you want the VM's interpreter loop to use computed gotos. 
    Computed gotos are only supported on certain compilers, so we have this
    small guard for that
    
    See this for more:
    http://gcc.gnu.org/onlinedocs/gcc-3.1.1/gcc/Labels-as-Values.html
*/
#ifndef LUG_COMPUTED_GOTO
    #if defined(_MSC_VER) && !defined(__clang__)
        #define LUG_COMPUTED_GOTO 0
    #else
        #define LUG_COMPUTED_GOTO 1
    #endif
#endif

// DEBUG FLAGS ------------------------------------------------------
// These flags are to be used only when working on the actual
// source code, and not to be used in production or release builds.
// These flags default to off.

// Set this to true to log garbage collections as they occur.
#define LUG_DEBUG_LOG_GC false

// Set this to true to stress test the GC. This will perform a
// collection before any allocation.
#define LUG_DEBUG_STRESS_GC false

// Set this to true to print the VM's stack state as it runs.
#define LUG_DEBUG_TRACE_EXECUTION false

// Set this to true to print out the compiled code of every chunk
// as soon as it finishes compiling.
#define LUG_DEBUG_PRINT_CODE false

#ifndef NDEBUG
    #include <cstdio>

    #define ASSERT(condition, message)                                                  \
        do                                                                              \
        {                                                                               \
            if (!(condition))                                                           \
            {                                                                           \
            std::fprintf(stderr, "[%s:%d] assert failed in %s(): %s\n",                 \
                __FILE__, __LINE__, __func__, message);                                 \
            std::abort();                                                               \
            }                                                                           \
        } while (false)

    #define UNREACHABLE()                                                               \
        do                                                                              \
        {                                                                               \
            std::fprintf(stderr, "[%s:%d] this code should not be reached in %s()\n",   \
                __FILE__, __LINE__, __func__);                                          \
            std::abort();                                                               \
        } while (false)
    
#else
    #define ASSERT(condition, message) /* empty */
    
    #if defined(_MSC_VER) && !defined(__clang__) // MSVC
        #define UNREACHABLE() __assume(false);
    #elif defined(__GNUC__) || defined(__clang__) // GCC, Clang
        #define UNREACHABLE() __builtin_unreachable();
    #else
        #define UNREACHABLE() /* empty */
    #endif

#endif