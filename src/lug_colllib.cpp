#include "lug_memory.h"
#include "lug_stdlib.h"
#include "lug_auxlib.h"

#include "lug_value.h"

namespace Luggage::Lib {
    

static Value Append(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 2);
    CheckType(vm, args[0], ObjType::LIST);

    ObjList* list = args[0].AsObj<ObjList>();
    Value value = args[1];

    list->Append(value);

    return Value::Nil();
}


static Value Clone(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ObjType::LIST);

    ObjList* list = args[0].AsObj<ObjList>();

    void* allocatedCloneMemory = Memory::Allocate<ObjList>(vm, 1);
    ObjList* clone = new (allocatedCloneMemory) ObjList(vm);
    vm.Push(clone); // We don't want a GC to collect this.

    // This creates a shallow copy.
    if (list->itemCount > 0) {
        clone->items = Memory::Allocate<Value>(vm, list->itemCapacity);
        clone->itemCapacity = list->itemCapacity;
        std::memcpy(clone->items, list->items, list->itemCount * sizeof(Value));
        clone->itemCount = list->itemCount;
    }

    vm.Pop();
    return clone;
}


// Library out ------------------------------

static constexpr NativeEntry listLib[] = {
    { "append",     &Append },
    { "clone",      &Clone },
};

// Function declaration of defined in lug_stdlib.h
void OpenCollection(VMCore& vm) { 
    ObjThing* listLibrary = NewLib(vm, "list");
    DefineLib(vm, listLibrary, listLib); 
}


}