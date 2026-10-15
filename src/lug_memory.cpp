#include <memory>

#include "lug_memory.h"
#include "lug_vm.h"

namespace Luggage::Memory {

void* Reallocate(VMCore& vm, void* pointer, std::size_t oldSize, std::size_t newSize) {
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

static void FreeObject(VMCore& vm, Obj* obj) {
    // Store the size before we call the destructor
    // since the destructor might modify the size or other members of the object...
    std::size_t size = obj->size; 
    obj->~Obj();
    Reallocate(vm, obj, size, 0);
}


void FreeObjects(VMCore& vm) {
    Obj* object = vm.objects;
    while (object != nullptr) {
        Obj* next = object->next;
        FreeObject(vm, object);
        object = next;
    }
}

}