#include "lug_memory.h"
#include "lug_stdlib.h"
#include "lug_auxlib.h"

#include "lug_value.h"

namespace Luggage::Lib {
    
namespace List {
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

    /*
        This creates a shallow copy.
    */
    ObjList* clone = ObjList::New(vm);
    for (std::size_t i = 0; i < list->items.Count(); i++) {
        clone->Append(list->items[i]);
    }

    return clone;
}


static Value Sum(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ObjType::LIST);

    ObjList* list = args[0].AsObj<ObjList>();

    int sum = 0;
    for (auto& item : *list) {
        if (!item.IsNumber()) return Value::Nil();

        sum += item.AsNumber();
    }

    return sum;
}


static Value Length(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ObjType::LIST);

    ObjList* list = args[0].AsObj<ObjList>();

    return static_cast<int>(list->items.Count());
}

}


namespace Dict {

static Value Clear(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ObjType::DICT);

    ObjDict* dict = args[0].AsObj<ObjDict>();
    
    for (auto& entry : dict->entries) {
        dict->Delete(entry.value);
    }

    return Value::Nil();
}


static Value Length(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ObjType::DICT);

    ObjDict* dict = args[0].AsObj<ObjDict>();

    return static_cast<int>(dict->entries.count);
}

}

// Library out ------------------------------

static constexpr NativeEntry listLib[] = {
    { "append",     &List::Append },
    { "clone",      &List::Clone },
    { "sum",        &List::Sum },
    { "length",        &List::Length },
};


static constexpr NativeEntry dictLib[] = {
    { "clear",      &Dict::Clear },
    { "length",     &Dict::Length }
};


// Function declaration of defined in lug_stdlib.h
void OpenCollection(VMCore& vm) { 
    ObjThing* listLibrary = NewLib(vm, "list");
    DefineLib(vm, listLibrary, listLib); 

    ObjThing* dictLibrary = NewLib(vm, "dict");
    DefineLib(vm, dictLibrary, dictLib);
}


}