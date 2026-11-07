#include "lug_memory.h"
#include "lug_stdlib.h"
#include "lug_auxlib.h"

#include "lug_value.h"

namespace Luggage::Lib {
    
// List methods ---------------------------------

namespace List {

static Value Append(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 2);
    CheckType(vm, args[0], ObjType::LIST);

    ObjList* list = args[0].AsObj<ObjList>();
    Value value = args[1];

    list->Append(value);

    return Value::Nil();
}


static Value Remove(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 2);
    CheckType(vm, args[0], ObjType::LIST);

    ObjList* list = args[0].AsObj<ObjList>();
    Value requestRemoved = args[1];

    for (std::size_t i = 0; i < list->Count(); i++) {
        const Value& item = (*list)[i];
        if (item == requestRemoved) {
            list->Pop(i);
            break;
        }
    }

    return Value::Nil();
}


static Value Insert(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 3);
    CheckType(vm, args[0], ObjType::LIST);
    CheckType(vm, args[1], ValueType::NUMBER);
    if (!args[1].IsInteger()) TypeError(vm, "integer", args[1].StringType());

    ObjList* list = args[0].AsObj<ObjList>();
    int index = args[1].AsInteger();
    const Value& value = args[2];

    list->Insert(index, value);

    return Value::Nil();
}


static Value Pop(VMCore& vm, Value* args, std::size_t argCount) {
    CheckType(vm, args[0], ObjType::LIST);
    ObjList* list = args[0].AsObj<ObjList>();
    
    // list.pop(list)
    if (argCount == 1) {
        return list->Pop(-1);
    }

    // list.pop(list, index)
    else if (argCount == 2) {
        if (!args[1].IsInteger()) TypeError(vm, "integer", args[1].StringType());
        return list->Pop(args[1].AsInteger());
    }
    
    ArgcError(vm, 1, argCount);
}


static Value Clone(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ObjType::LIST);

    ObjList* list = args[0].AsObj<ObjList>();
    return list->Copy();
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

    return static_cast<int>(list->Count());
}


static Value Get(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 2);
    CheckType(vm, args[0], ObjType::LIST);
    CheckType(vm, args[1], ValueType::NUMBER);

    ObjList* list = args[0].AsObj<ObjList>();   
    if (!args[1].IsInteger()) {
        TypeError(vm, "integer", args[1].StringType());
    }

    int index = args[1].AsInteger();
    const Value* value = list->Get(index);

    if (value == nullptr) return Value::Nil();

    return *value;
}


static Value Index(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 2);
    CheckType(vm, args[0], ObjType::LIST);
    
    ObjList* list = args[0].AsObj<ObjList>();
    Value& value = args[1];

    for (int i = 0; i < list->Count(); i++) {
        const Value* item = list->Get(i);
        ASSERT(item != nullptr, "index has gone out of bounds");
        if (*item == value) return i;
    }

    return Value::Nil();
}


static Value Concat(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 2);
    CheckType(vm, args[0], ObjType::LIST);
    CheckType(vm, args[1], ObjType::LIST);

    ObjList* listA = args[0].AsObj<ObjList>();
    ObjList* listB = args[1].AsObj<ObjList>();

    ObjList* newList = ObjList::New(vm);
    vm.stack.Push(newList);

    for (auto& item : *listA) {
        newList->Append(item);
    }

    for (auto& item : *listB) {
        newList->Append(item);
    }

    vm.stack.Pop(); // newList

    return newList;
}


static Value Clear(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ObjType::LIST);

    ObjList* list = args[0].AsObj<ObjList>();
    list->items->data.Clear();

    return Value::Nil();
}


static Value Join(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 2);
    CheckType(vm, args[0], ObjType::LIST);
    CheckType(vm, args[1], ObjType::STRING);

    ObjList* list = args[0].AsObj<ObjList>();
    ObjString* separator = args[1].AsObj<ObjString>();

    // Create a buffer to hold the joined string together
    Vector<char> joinBuffer(vm);

    for (std::size_t i = 0; i < list->Count(); i++) {
        if (i > 0) joinBuffer.Append(separator->characters, separator->length);

        ObjString* s = (*list)[i].ToString(vm);
        vm.stack.Push(s);                                // root across a possible GC in Append
        joinBuffer.Append(s->characters, s->length);
        vm.stack.Pop();
    }

    return ObjString::FromBuffer(vm, joinBuffer);
}

}

// Dictionary methods ---------------------------

namespace Dict {

static Value Clear(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ObjType::DICT);

    ObjDict* dict = args[0].AsObj<ObjDict>();
    
    for (auto& entry : dict->entries) {
        dict->Delete(entry.key);
    }

    return Value::Nil();
}


static Value Length(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ObjType::DICT);

    ObjDict* dict = args[0].AsObj<ObjDict>();

    return static_cast<int>(dict->entries.count);
}


static Value Get(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 2);
    CheckType(vm, args[0], ObjType::DICT);

    ObjDict* dict = args[0].AsObj<ObjDict>();
    Value key = args[1];
    const Value* value = dict->Get(key);

    if (value == nullptr) return Value::Nil();

    return *value;
}


static Value Clone(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ObjType::DICT);

    ObjDict* dict = args[0].AsObj<ObjDict>();

    return dict->Copy();
}


static Value Remove(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 2);
    CheckType(vm, args[0], ObjType::DICT);

    ObjDict* dict = args[0].AsObj<ObjDict>();
    Value& keyToRemove = args[1];
    
    dict->Delete(keyToRemove);

    return Value::Nil();
}


static Value Values(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ObjType::DICT);

    ObjDict* dict = args[0].AsObj<ObjDict>();
    ObjList* values = ObjList::New(vm);
    vm.stack.Push(values);

    for (auto& entry : *dict) {
        values->Append(entry.value);
    } 

    vm.stack.Pop();

    return values;
}


static Value Keys(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ObjType::DICT);

    ObjDict* dict = args[0].AsObj<ObjDict>();
    ObjList* keys = ObjList::New(vm);
    vm.stack.Push(keys);

    for (auto& entry : *dict) {
        keys->Append(entry.key);
    }

    vm.stack.Pop();

    return keys;
}


static Value Has(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 2);
    CheckType(vm, args[0], ObjType::DICT);

    ObjDict* dict = args[0].AsObj<ObjDict>();
    Value& has = args[1];

    return dict->Get(has) != nullptr ? true : false;
}

}

// Library out ------------------------------

static constexpr NativeEntry listLib[] = {
    { "append",     &List::Append },
    { "remove",     &List::Remove },
    { "insert",     &List::Insert },
    { "pop",        &List::Pop },
    { "clone",      &List::Clone },
    { "sum",        &List::Sum },
    { "length",     &List::Length },
    { "get",        &List::Get },
    { "index",      &List::Index },
    { "concat",     &List::Concat },
    { "clear",      &List::Clear },
    { "join",       &List::Join },
};


static constexpr NativeEntry dictLib[] = {
    { "clear",      &Dict::Clear },
    { "length",     &Dict::Length },
    { "get",        &Dict::Get },
    { "clone",      &Dict::Clone },
    { "remove",     &Dict::Remove },
    { "values",     &Dict::Values },
    { "keys",       &Dict::Keys },
    { "has",        &Dict::Has },
};


// Function declaration of defined in lug_stdlib.h
void OpenCollection(VMCore& vm) { 
    ObjThing* listLibrary = NewLib(vm, "list");
    DefineLib(vm, listLibrary, listLib); 

    ObjThing* dictLibrary = NewLib(vm, "dict");
    DefineLib(vm, dictLibrary, dictLib);
}


}