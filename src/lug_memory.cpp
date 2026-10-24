#include <memory>

#include "lug_memory.h"
#include "lug_vm.h"

#ifdef LUG_DEBUG_LOG_GC
#include <cstdio>
#include "lug_debug.h"
#endif

namespace Luggage::Memory {

void* Reallocate(VMCore& vm, void* pointer, std::size_t oldSize, std::size_t newSize) {
    GC& gc = vm.gc;
    gc.bytesAllocated += newSize - oldSize;
    
    if (newSize > oldSize) {
        #ifdef LUG_DEBUG_STRESS_GC
        vm.CollectGarbage();
        #endif
    }

    if (newSize > oldSize && gc.bytesAllocated > gc.nextGC) {
        gc.Collect();
    }

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