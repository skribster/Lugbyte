#pragma once

// ESSENTIAL HEADERS -------------------------------------------
// std::uintx_t
#include <cstdint>
// std::size_t
#include <cstddef>
// std::abort, std::malloc, std::calloc, etc.
#include <cstdlib>


#define UINT8_COUNT UINT8_MAX + 1


#if defined(__clang__) || defined(__GNUC__) 
    #define USE_COMPUTED_GOTO
#endif


#ifndef NDEBUG
    #include <cstdio>

    #define LUG_DEBUG_TRACE_EXECUTION
    #define LUG_DEBUG_PRINT_CODE

    #define ASSERT(condition, message)                                                  \
        do                                                                              \
        {                                                                               \
            if (!(condition))                                                           \
            {                                                                           \
            std::fprintf(stderr, "[%s:%d] Assert failed in %s(): %s\n",                 \
                __FILE__, __LINE__, __func__, message);                                 \
            std::abort();                                                               \
            }                                                                           \
        } while (false)

    #define UNREACHABLE()                                                               \
        do                                                                              \
        {                                                                               \
            std::fprintf(stderr, "[%s:%d] This code should not be reached in %s()\n",   \
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