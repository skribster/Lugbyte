#include <ctime>

#include "luggage.hpp"
#include "lug_stdlib.h"
#include "lug_auxlib.h"
#include "lug_value.h"

namespace Luggage::Lib {


// Library implementation functions -----------------------------

static Value Write(VMCore& vm, Value* args, std::size_t argCount) {
    for (std::size_t i = 0; i < argCount; i++) {
        if (i > 0) std::printf(" ");

        Value value = args[i];
        ObjString* stringed = vm.ToString(value);
        ASSERT(stringed != nullptr, "Couldn't convert argument to string");

        std::printf("%s", stringed->characters);
    }

    return Value::Nil();
}


static Value Print(VMCore& vm, Value* args, std::size_t argCount) {
    Write(vm, args, argCount);
    std::printf("\n");

    return Value::Nil();
}


static Value Clock(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 0);

    std::clock_t currentTime = std::clock();
    double cpuTimeUsed = static_cast<double>(currentTime) / CLOCKS_PER_SEC;
    return Value(cpuTimeUsed);
}


static Value Thing(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 0);

    ObjThing* thing = ObjThing::New(vm);
    return thing;
}


static Value Error(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ObjType::STRING);

    ObjString* errorMessage = args[0].AsObj<ObjString>();
    vm.RuntimeError(errorMessage->characters);
    
    return 0;
}


static Value Pcall(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ObjType::CLOSURE);

    ObjClosure* function = args[0].AsObj<ObjClosure>();
    ObjThing* result = ObjThing::New(vm);
    vm.Push(result);

    /*
        When we error, we want to rewind to the saved state of the
        stack. Keep the old state here and if fail, set stackTop and
        frameCount back to its originals.
    */
    Value* oldTop = vm.stackTop;
    std::size_t oldFrameCount = vm.frameCount;

    try {
        vm.Push(function);
        vm.Call(function, 0);
        result->Set("success", true);
        result->Set("result", Value::Nil());

    } catch (LugError& error) {
        const char* errorMessage = error.message.c_str();
        
        result->Set("success", false);
        result->Set("result", vm.Peek(0));

        /*
            When the pcall ends, we want to make sure that any upvalues
            will get closed. Normally, the body in OP_RETURN closes the
            upvalues for us if ran successfully, but we have to do it 
            manually here.

            Say we have a program like;

                box saved
                pcall(function()
                    box x <- 10

                    saved <- function() return x end

                    error("this will error")
                end)

                saved()

            x is an upvalue which captures the outer 'x'
            of the function. right now, it points to the stack
            slot of 'x', but when pcall errors, the slot
            that 'saved' is pointing to is no longer there.
            the end saved() will try and read the stack slot of x,
            that it's pointing to, but it's already been freed.
        */
        vm.CloseUpvalues(oldTop);
        vm.frameCount = oldFrameCount;
        vm.stackTop = oldTop;

    }

    return vm.Pop();
}


static Value Type(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    
    // Value::StringType() returns a literal.
    const char* stringType = args[0].StringType();
    return ObjString::FromLiteral(vm, stringType);
}


static Value Deepcopy(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    return vm.Deepcopy(args[0]);
}


// Library out ------------------------------

static constexpr NativeEntry baseLib[] = {
    { "write",      &Write },
    { "print",      &Print },
    { "clock",      &Clock },
    { "error",      &Error },
    { "pcall",      &Pcall },
    { "deepcopy",   &Deepcopy },
    { "type",       &Type },
};

// Function declaration of defined in lug_stdlib.h
void OpenBase(VMCore& vm) { 
    DefineLib(vm, nullptr, baseLib); 
}

}