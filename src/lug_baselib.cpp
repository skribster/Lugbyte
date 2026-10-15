#include <ctime>

#include "lug_stdlib.h"
#include "lug_value.h"

namespace Luggage::Lib {


// Library implementation functions -----------------------------

static Value Print(VMCore& vm, Value* args, std::size_t argCount) {
    if (argCount != 1) {
        vm.RuntimeError("Expected %d argument(s), got %d", 1, argCount);
    }

    Value::Print(args[0]);
    std::printf("\n");

    return Value::Nil();
}


static Value Clock(VMCore& vm, Value* args, std::size_t argCount) {
    if (argCount != 0) {
        vm.RuntimeError("Expected %d argument(s), got %d", 0, argCount);
    }

    std::clock_t currentTime = std::clock();
    double cpuTimeUsed = static_cast<double>(currentTime) / CLOCKS_PER_SEC;
    return Value(cpuTimeUsed);
}


static Value Thing(VMCore& vm, Value* args, std::size_t argCount) {
    if (argCount != 0) {
        vm.RuntimeError("Expected %d argument(s), got %d", 0, argCount);
    }

    ObjThing* thing = ObjThing::New(vm);
    return thing;
}

// Library out ------------------------------

static constexpr NativeEntry baseLib[] = {
    { "print",      &Print },
    { "clock",      &Clock },
    { "thing",      &Thing }, 
};

// Function declaration of defined in lug_stdlib.h
void OpenBase(VMCore& vm) { DefineLib(vm, nullptr, baseLib); }

}