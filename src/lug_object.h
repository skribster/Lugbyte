#pragma once

#include <filesystem> 

#include "lug_common.h"
#include "lug_value.h"
#include "lug_chunk.h"
#include "lug_table.h"
#include "lug_memory.h"

namespace Luggage {

// Forward declaration so C++ doesn't get mad at me
class VMCore;
class GC;

enum class ObjType {
    OBJ,
    STRING,
    FUNCTION,
    MODULE,
    UPVALUE,
    CLOSURE,
    NATIVE,
    LIST,
    DICT,
    THING,
    CATEGORY,
};


struct Obj {
    Obj(VMCore& vm, ObjType type) : vm(vm), type(type) {}
    virtual ~Obj() = default;
    virtual void Print();
    virtual ObjString* ToString();
    virtual void Blacken(GC& gc) = 0;
    virtual Obj* Copy();

    static const char* StringType(ObjType type);

    VMCore& vm;
    ObjType type;
    std::uint32_t hash;
    Obj* next{ nullptr };       // Points to the next Obj in the Obj linked list.
    std::size_t size{ 0 };      // Size of the Obj.
    bool isMarked{ false };
};

// ObjString -----------------

struct ObjString : Obj {
    explicit ObjString(VMCore& vm) : Obj(vm, ObjType::STRING) {}    
    ~ObjString() override;
    void Print() override;
    ObjString* ToString() override;
    void Blacken(GC& gc) override {} // Nothing to blacken

    ObjString* Sub(int start, int end);
    ObjString* Concatenate(ObjString* other);
    const char* CharAt(int index);

    // Factory methods to create a new ObjString on the heap
    static ObjString* FromSource(VMCore& vm, const char* original, std::size_t length);
    static ObjString* FromLiteral(VMCore& vm, const char* literal);
    static ObjString* FromAllocated(VMCore& vm, char* chars, std::size_t length);
    static ObjString* FromFormat(VMCore& vm, const char* format, ...);

    char* characters{ nullptr };
    std::size_t length{ 0 };
};


// ObjFunction -----------------

enum class FunctionType {
    FUNCTION,
    SCRIPT
};

struct ObjFunction : Obj {
    struct Arguments {
        static constexpr std::size_t MAX_ARITY{ UINT8_MAX };

        std::uint8_t arity{ 0 };
        std::uint8_t shared{ 0 };

        inline void MarkShared(std::size_t argIndex) {
            shared |= (1 << argIndex);
        }
    };

    explicit ObjFunction(VMCore& vm) : Obj(vm, ObjType::FUNCTION), chunk(vm) {}
    ~ObjFunction() override;
    void Print() override;
    void Blacken(GC& gc) override;
    
    static ObjFunction* New(VMCore& vm);

    Arguments parameters;
    std::size_t upvalueCount{ 0 };
    ObjString* name{ nullptr };
    Chunk chunk;
};

// ObjModule -----------------

struct ObjModule : Obj {
    explicit ObjModule(VMCore& vm) : Obj(vm, ObjType::MODULE), globals(vm) {}
    void Print() override;
    void Blacken(GC& gc) override;

    Table globals;
    ObjFunction* function{ nullptr };
    ObjString* path{ nullptr };

    static ObjModule* New(VMCore& vm, ObjFunction* function, std::filesystem::path path); 
};

// ObjUpvalue -----------------

struct ObjUpvalue : Obj {
    explicit ObjUpvalue(VMCore& vm) : Obj(vm, ObjType::UPVALUE) {}
    void Print() override;
    void Blacken(GC& gc) override;

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
    ObjString* ToString() override;
    void Blacken(GC& gc) override;

    ObjFunction* function{ nullptr };
    ObjModule* module{ nullptr };

    // Upvalue array
    ObjUpvalue** upvalues{ nullptr };
    std::size_t upvalueCount{ 0 };

    static ObjClosure* New(VMCore& vm, ObjFunction* function, ObjModule* module);
};

// ObjNative -----------------

using NativeFn = Value(*)(VMCore& vm, Value* args, std::size_t argCount);

struct ObjNative : Obj {
    explicit ObjNative(VMCore& vm) : Obj(vm, ObjType::NATIVE) {}
    void Print() override;
    ObjString* ToString() override;
    void Blacken(GC& gc) override {};

    NativeFn function{ nullptr };
    const char* name;

    static ObjNative* New(VMCore& vm, const char* name, NativeFn function);
};

// ObjList -----------------

struct ItemBuffer {
    ItemBuffer(VMCore& vm) : vm(vm), data(vm) {}

    VMCore& vm;
    std::size_t refCount{ 1 };
    Vector<Value> data;
};


struct ObjList : Obj {
    explicit ObjList(VMCore& vm) : Obj(vm, ObjType::LIST) {}
    ~ObjList() override;
    void Print() override;
    ObjString* ToString() override;
    void Blacken(GC& gc) override;
    Obj* Copy() override;
    
    // An ObjList is just a wrapper for a dynamic array.
    ItemBuffer* items{ nullptr };
    void MakeUnique();
    
    void Append(Value value);
    Value* Get(int index);
    void Set(int index, Value value);
    void Insert(int index, Value value);
    Value Pop(int index);
    int Count() const;
    ObjList* Slice(int start, int end);

    Vector<Value>::Iterator begin() { return items->data.begin(); }
    Vector<Value>::Iterator end() { return items->data.end(); }

    const Value& operator[](int index) { return *Get(index); }

    static ObjList* New(VMCore& vm);
};


// ObjDict -----------------

struct ObjDict : Obj {
    explicit ObjDict(VMCore& vm) 
        : Obj(vm, ObjType::DICT), entries(vm) 
    {}

    void Print() override;
    ObjString* ToString() override;
    void Blacken(GC& gc) override;
    Obj* Copy() override;

    Table entries;

    const Value* Get(const Value& key) const;
    void Set(Value key, Value value);
    void Delete(Value key);

    Table::Iterator begin() { return entries.begin(); }
    Table::Iterator end()   { return entries.end(); }

    static ObjDict* New(VMCore& vm);
};

// ObjThing -----------------

struct ObjThing : Obj {
    explicit ObjThing(VMCore& vm)
        : Obj(vm, ObjType::THING), attributes(vm) 
    {}

    void Print() override;
    ObjString* ToString() override;
    void Blacken(GC& gc) override;
    Obj* Copy() override;

    Table attributes;
    
    const Value* Get(ObjString* attribute) const;
    const Value* Get(const char* attribute) const;
    void Set(ObjString* attribute, Value value);
    // Small shortcut for setting a string field.
    void Set(const char* attribute, Value value);
    void Delete(Value attribute);

    Table::Iterator begin() { return attributes.begin(); }
    Table::Iterator end()   { return attributes.end(); }

    static ObjThing* New(VMCore& vm);
};

// ObjCategory -----------------

struct ObjCategory : Obj {
    explicit ObjCategory(VMCore& vm)
        : Obj(vm, ObjType::CATEGORY), types(vm)
    {}

    void Print() override;
    ObjString* ToString() override;
    void Blacken(GC& gc) override;
    
    void AddType(ObjString* typeName, Value& index);
    const Value* GetType(ObjString* typeName);
    
    Table types;
    int currentIndex{ 0 };
    
    static ObjCategory* New(VMCore& vm);
};

}