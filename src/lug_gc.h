#pragma once

#include "lug_value.h"
#include "lug_object.h"
#include "lug_table.h"
#include "lug_vector.h"

namespace Luggage { 

// Forward declarations so C++ doesn't get mad at me
class VMCore; 
class Compiler;

class GC {
public:
    explicit GC(VMCore& vm) : vm(vm) {}
    ~GC();

    void Collect();
    void MarkValue(Value& value);
    void MarkObject(Obj* object);
    void MarkTable(Table& table);
    void MarkVector(Vector<Value>& vector);
    
    void AddObject(Obj* object);
    void FreeObjects();
    void FreeObject(Obj* object);

    std::size_t bytesAllocated{ 0 };
    std::size_t nextGC{ 1024 * 1024 };
    static constexpr std::size_t HEAP_GROW_FACTOR{ 2 };
    
    Obj* objects{ nullptr };

    Obj** grayStack{ nullptr };
    std::size_t grayCapacity{ 0 };
    std::size_t grayCount{ 0 };

private:
    void TraceReferences();
    void BlackenObject(Obj* obj);
    void Sweep();

    VMCore& vm;
};

}