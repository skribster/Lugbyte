#include <cstdio>

#include "lug_object.h"
#include "lug_memory.h"
#include "lug_vm.h"

namespace Luggage {

// Helper functions ------------------------------------

template<typename T>
static T* AllocateObject(VMCore& vm) {
    /*
        Every heap allocation needs to go through Reallocate for the 
        garbage collector. We cannot do 'new <Object>' to allocate
        to the heap, since it won't count the number of bytes we 
        allocated and add it to the vm's bytesAllocated stat, 
        so we route it to this function.
    */
    void* allocatedMemory = Memory::Reallocate(vm, nullptr, 0, sizeof(T));
    
    // This syntax allows us to create the Obj at the given
    // memory block. This also calls the constructor of
    // the Obj, which standard malloc() / Reallocate() does 
    // not do.
    T* object = new (allocatedMemory) T(vm);

    object->size = sizeof(T);
    object->next = vm.objects;
    vm.objects = object;

    return object;
}


// Obj methods ------------------------------------
void Obj::Print(Obj* obj) {
    switch (obj->type) {
        case ObjType::STRING: {
            ObjString* string = static_cast<ObjString*>(obj);
            std::printf(string->characters);

            break;
        }

        default: {
            std::printf("Unknown object");
            UNREACHABLE();
            break;
        }
    }
}


// ObjString methods ------------------------------------

static std::uint32_t HashString(const char* string, std::size_t length) {
    std::uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= static_cast<std::uint8_t>(string[i]);
        hash *= 16777619;
    }

    return hash;
}


/**
 * Allocates an ObjString* onto the heap and returns its memory
 * address.
 */
static ObjString* AllocateString(VMCore& vm, char* characters, std::size_t length, std::uint32_t hash) {
    ObjString* string = AllocateObject<ObjString>(vm);
    string->length = length;
    string->characters = characters;
    string->hash = hash;

    // Add the string to the vm's interned string table
    vm.strings.Set(string, Value::Nil());

    return string;
}

/**
 * Creates an ObjString from a const-string, usually part of
 * the source code itself. This method will create a new heap
 * allocation for the passed-in string, since it cannot take
 * ownership of the original one.
 */
ObjString* ObjString::CopyFrom(VMCore& vm, const char* original, std::size_t length) {
    std::uint32_t hash = HashString(original, length);

    // Check if the string is already in the vm's string
    // interned hashset. If so, we don't need to allocate
    // anything at all, we can just return it!
    ObjString* interned = vm.strings.FindString(original, length, hash);
    if (interned != nullptr) return interned;
    
    /*
        Note that we allocate one extra byte of memory to
        store the null terminator at the end of the string.
        When we pass in the lexeme of a token in copyString(),
        the lexeme is part of the greater source string itself,
        which means it's not terminated.

        var variable = 5;
            ^^^^^^^^
            const char* chars = ['v', 'a', 'r', 'i', ...] (no \0)
    */
    char* heapChars = Memory::Allocate<char>(vm, length + 1);
    
    // We'll then copy the original characters onto our
    // newly allocated space.
    std::memcpy(heapChars, original, length);

    // Don't forget the null terminator.
    heapChars[length] = '\0';

    return AllocateString(vm, heapChars, length, hash);
}

/**
 * Instead of copying the characters/allocating them to a new
 * block of memory on the heap like in ObjString::CopyFrom, this function
 * claims ownership of the string you give it.
 */
ObjString* ObjString::TakeFrom(VMCore& vm, char* chars, std::size_t length) {
    std::uint32_t hash = HashString(chars, length);
    
    // Check if the string already exists in the 
    // vm's interned-string hashset
    ObjString* interned = vm.strings.FindString(chars, length, hash);
    if (interned != nullptr) {
        Memory::FreeArray<char>(vm, chars, length + 1);
        return interned;
    }

    return AllocateString(vm, chars, length, hash);
}


ObjString::~ObjString() {
    // +1 for the null terminator at the end
    Memory::FreeArray<char>(vm, characters, length + 1);
}


}