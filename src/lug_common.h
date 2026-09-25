#pragma once

// std::uintx_t
#include <cstdint>

// std::size_t
#include <cstddef>

#define LUG_DEBUG_TRACE_EXECUTION
#define LUG_DEBUG_PRINT_CODE

#if defined(__clang__) || defined(__GNUC__) 
    #define USE_COMPUTED_GOTO
#endif