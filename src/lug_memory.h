#pragma once

#include "lug_common.h"

namespace Luggage {
    class VMCore; // Forward declare VMCore
}


namespace Luggage::Memory {


/**
 * The 'god' function of reallocating memory on the heap.
 * This function is crucial to the GC as it also regulates 
 * garbage collection for the VM.
 * 
 * if oldSize == 0 and newSize != 0, allocate a new block
 * if oldSize != 0 and newSize == 0, free the allocation
 * if oldSize != 0 and newSize < oldSize, shrink allocation
 * if oldSize != 0 and newSize > oldSize, grow allocation
 */
void* Reallocate(VMCore& vm, void* pointer, std::size_t oldSize, std::size_t newSize);

/**
 * Grows a dynamic array to the new capacity.
 */
template <typename T>
[[nodiscard]] T* GrowArray(VMCore& vm, T* array, std::size_t oldCapacity, std::size_t newCapacity) {
    return (T*)Reallocate(vm, array, sizeof(T) * oldCapacity, sizeof(T) * newCapacity);
}

/**
 * Frees a dynamic array from memory.
 */ 
template<typename T>
T* FreeArray(VMCore& vm, T* array, std::size_t currentCapacity) {
    // We route freeing to the Reallocate function, but just set
    // the new size to 0. We have to route all memory functions to
    // Reallocate because the VM needs to keep track of how mnay bytes
    // it allocates so it knows when to garbage collect.
    return (T*)Reallocate(vm, array, sizeof(T) * currentCapacity, 0);
}


/**
 * Allocates a memory block of given type with
 * a given count.
 */
template<typename T>
[[nodiscard]] T* Allocate(VMCore& vm, std::size_t count) {
    return (T*)Reallocate(vm, nullptr, 0, sizeof(T) * count);
}


template<typename T>
void Free(VMCore& vm, T* pointer) {
    pointer->~T();
    Reallocate(vm, pointer, sizeof(T), 0);
}

/**
 * Calculates a new capacity based on a given
 * current capacity. The minimum capacity is
 * 8 elements.
 */
constexpr std::size_t GrowCapacity(std::size_t capacity) {
    return capacity < 8 ? 8 : capacity * 2;
}

}