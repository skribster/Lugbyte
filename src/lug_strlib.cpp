#include <cctype>

#include "lug_stdlib.h"
#include "lug_auxlib.h"
#include "lug_value.h"
#include "lug_memory.h"


namespace Luggage::Lib {

static Value Upper(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ObjType::STRING);

    ObjString* string = args[0].AsObj<ObjString>();
    char* upperString = Memory::Allocate<char>(vm, string->length + 1);

    for (std::size_t i = 0; i < string->length; i++) {
        upperString[i] = std::toupper(static_cast<unsigned char>(string->characters[i]));
    }

    upperString[string->length] = '\0';

    return ObjString::FromAllocated(vm, upperString, string->length);
}


static Value Lower(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ObjType::STRING);

    ObjString* string = args[0].AsObj<ObjString>();
    char* upperString = Memory::Allocate<char>(vm, string->length + 1);

    for (std::size_t i = 0; i < string->length; i++) {
        upperString[i] = std::tolower(static_cast<unsigned char>(string->characters[i]));
    }

    upperString[string->length] = '\0';

    return ObjString::FromAllocated(vm, upperString, string->length);
}


static Value Length(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ObjType::STRING);

    ObjString* string = args[0].AsObj<ObjString>();

    return static_cast<int>(string->length);
}


static Value __Call__(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    return args[0].ToString(vm);
}


// Library out ------------------------------

static constexpr NativeEntry stringLib[] = {
    { "upper",      &Upper },
    { "lower",      &Lower },
    { "length",     &Length },
    { "__call__",   &__Call__ }
};

// Function declaration of defined in lug_stdlib.h
void OpenString(VMCore& vm) { 
    ObjThing* library = NewLib(vm, "string");
    DefineLib(vm, library, stringLib);  
}


}