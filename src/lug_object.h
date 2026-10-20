#pragma once

#include "lug_common.h"
#include "lug_value.h"
#include "lug_chunk.h"
#include "lug_table.h"
#include "lug_memory.h"

namespace Luggage {

// Forward declaration so C++ doesn't get mad at me
class VMCore;

enum class ObjType {
    OBJ,
    STRING,
    FUNCTION,
    UPVALUE,
    CLOSURE,
    NATIVE,
    LIST,
    DICTIONARY,
    THING,
};


struct Obj {
    Obj(VMCore& vm, ObjType type) : vm(vm), type(type) {}
    virtual ~Obj() = default;
    virtual void Print();

    static const char* StringType(ObjType type);

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
    void Print() override;

    // Factory methods to create a new ObjString on the heap
    static ObjString* FromSource(VMCore& vm, const char* original, std::size_t length);
    static ObjString* FromLiteral(VMCore& vm, const char* literal);
    static ObjString* FromAllocated(VMCore& vm, char* chars, std::size_t length);
    static ObjString* FromFormat(VMCore& vm, const char* format, ...);

    char* characters{ nullptr };
    std::size_t length{ 0 };
};


// ObjFunction -----------------

struct ObjFunction : Obj {
    explicit ObjFunction(VMCore& vm) : Obj(vm, ObjType::FUNCTION), chunk(vm) {}
    ~ObjFunction() override;
    void Print() override;

    std::size_t arity{ 0 };
    std::size_t upvalueCount{ 0 };
    ObjString* name{ nullptr };
    Chunk chunk;

    static ObjFunction* New(VMCore& vm);
};

// ObjUpvalue -----------------

struct ObjUpvalue : Obj {
    explicit ObjUpvalue(VMCore& vm) : Obj(vm, ObjType::UPVALUE) {}
    ~ObjUpvalue() override;
    void Print() override;

    // We use a pointer here since upvalues store a *reference*
    // to the actual variable,not a copy.
    Value* location{ nullptr };
    Value closed{ Value::Nil() };
    ObjUpvalue* next{ nullptr };

    static ObjUpvalue* New(VMCore& vm, Value* slot);
};

// ObjClosure -----------------

struct ObjClosure : Obj {
    explicit ObjClosure(VMCore& vm) : Obj(vm, ObjType::CLOSURE) {}
    ~ObjClosure() override;
    void Print() override;

    ObjFunction* function{ nullptr };

    // Upvalue array
    ObjUpvalue** upvalues{ nullptr };
    std::size_t upvalueCount{ 0 };

    static ObjClosure* New(VMCore& vm, ObjFunction* function);
};

// ObjNative -----------------

using NativeFn = Value(*)(VMCore& vm, Value* args, std::size_t argCount);

struct ObjNative : Obj {
    explicit ObjNative(VMCore& vm) : Obj(vm, ObjType::NATIVE) {}
    ~ObjNative() override;
    void Print() override;

    NativeFn function{ nullptr };
    const char* name;

    static ObjNative* New(VMCore& vm, const char* name, NativeFn function);
};

// ObjList -----------------

struct ObjList : Obj {
    explicit ObjList(VMCore& vm) : Obj(vm, ObjType::LIST) {}
    ~ObjList() override;
    void Print() override;

    // An ObjList is just a wrapper for a dynamic array.
    Value* items{ nullptr };
    std::size_t itemCount{ 0 };
    std::size_t itemCapacity{ 0 };

    void Append(Value value);
    Value& Get(int index) const;
    void Set(int index, Value value);

    static ObjList* New(VMCore& vm);
};


// ObjDict -----------------

struct ObjDict : Obj {
    explicit ObjDict(VMCore& vm) 
        : Obj(vm, ObjType::DICTIONARY), dictionary(vm) 
    {}

    ~ObjDict() override;
    void Print() override;

    Table dictionary;

    Value Get(Value key) const;
    void Set(Value key, Value value);
    void Delete(Value key);

    static ObjDict* New(VMCore& vm);
};

// ObjThing -----------------

struct ObjThing : Obj {
    explicit ObjThing(VMCore& vm)
        : Obj(vm, ObjType::THING), attributes(vm) 
    {}

    ~ObjThing() override;
    void Print() override;

    Table attributes;
    
    Value Get(Value field) const;
    Value Get(const char* field) const;
    void Set(Value key, Value field);
    // Small shortcut for setting a string field.
    void Set(const char* key, Value field);

    static ObjThing* New(VMCore& vm);
};

}