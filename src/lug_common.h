#pragma once

// std::uintx_t
#include <cstdint>
// std::size_t
#include <cstddef>
// std::abort, std::malloc, std::calloc, etc.
#include <cstdlib>

// #define LUG_DEBUG_TRACE_EXECUTION
// #define LUG_DEBUG_PRINT_CODE
#define UINT8_COUNT UINT8_MAX + 1


#if defined(__clang__) || defined(__GNUC__) 
    #define USE_COMPUTED_GOTO
#endif


#if defined(NDEBUG) || defined(_DEBUG)
    #include <cstdio>

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
    #define ASSERT(condition, message) do { } while (false);
    #define UNREACHABLE()

#endif