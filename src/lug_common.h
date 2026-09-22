#pragma once

// std::uintx_t
#include <cstdint>

// std::size_t
#include <cstddef>

#define ASSERT(condition, message)                                      \
    do                                                                  \
    {                                                                   \
    if (!(condition))                                                   \
    {                                                                   \
        fprintf(stderr, "[%s:%d] Assert failed in %s(): %s\n",          \
            __FILE__, __LINE__, __func__, message);                     \
        abort();                                                        \
    }                                                                   \
    } while (false)