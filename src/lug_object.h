#pragma once

#include "lug_common.h"
#include "lug_value.h"

namespace Luggage {

// Forward declaration so C++ doesn't get mad at me
class VMCore;

enum class ObjType {
    OBJ,
    STRING,
    FUNCTION,
    LIST,
    DICTIONARY
};

struct Obj {
    Obj(VMCore& vm, ObjType type) : vm(vm), type(type) {}
    virtual ~Obj() = default;
    static void Print(Obj* obj);

    VMCore& vm;
    ObjType type;
    std::uint32_t hash;
    Obj* next{ nullptr };       // Points to the next Obj in the Obj linked list.
    std::size_t size{ 0 };      // Size of the Obj.
};


struct ObjString : Obj {
    ObjString(VMCore& vm) : Obj(vm, ObjType::STRING) {}    
    ~ObjString() override;

    // Factory methods to create a new ObjString on the heap
    static ObjString* CopyFrom(VMCore& vm, const char* original, std::size_t length);
    static ObjString* TakeFrom(VMCore& vm, char* chars, std::size_t length);

    char* characters{ nullptr };
    std::size_t length{ 0 };
};


}