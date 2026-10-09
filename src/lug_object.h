#pragma once

#include "lug_common.h"
#include "lug_value.h"
#include "lug_chunk.h"

namespace Luggage {

// Forward declaration so C++ doesn't get mad at me
class VMCore;

enum class ObjType {
    OBJ,
    STRING,
    FUNCTION,
    NATIVE,
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

// ObjString -----------------

struct ObjString : Obj {
    explicit ObjString(VMCore& vm) : Obj(vm, ObjType::STRING) {}    
    ~ObjString() override;

    // Factory methods to create a new ObjString on the heap
    static ObjString* CopyFrom(VMCore& vm, const char* original, std::size_t length);
    static ObjString* TakeFrom(VMCore& vm, char* chars, std::size_t length);

    char* characters{ nullptr };
    std::size_t length{ 0 };
};

// ObjFunction -----------------

struct ObjFunction : Obj {
    explicit ObjFunction(VMCore& vm) : Obj(vm, ObjType::FUNCTION), chunk(vm) {}
    ~ObjFunction() override;

    void BookkeepLocal(
        const char* name, 
        std::size_t nameLength
    );

    std::size_t arity{ 0 };
    Chunk chunk;
    ObjString* name;

    static ObjFunction* New(VMCore& vm);
    void PrintFunction();
};


// ObjNative -----------------

using NativeFn = Value(*)(VMCore& vm, Value* args, std::size_t argCount);

struct ObjNative : Obj {
    explicit ObjNative(VMCore& vm) : Obj(vm, ObjType::NATIVE) {}
    ~ObjNative() override;

    NativeFn function{ nullptr };

    static ObjNative* New(VMCore& vm, NativeFn function);
};

}