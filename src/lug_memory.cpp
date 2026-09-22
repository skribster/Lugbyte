#include <cstdlib>
#include <memory>

#include "lug_memory.h"

namespace Luggage::Memory {

void* Reallocate(void* pointer, std::size_t oldSize, std::size_t newSize) {
    if (newSize == 0) {
        std::free(pointer);
        return nullptr;
    }

    void* resized = std::realloc(pointer, newSize);

    // If allocation failed, we will throw a
    // bad_alloc error.
    if (resized == nullptr) {
        throw std::bad_alloc();
    }
    
    return resized;
}

}