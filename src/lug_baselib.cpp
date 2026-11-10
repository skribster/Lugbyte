#include <ctime>


#include "luggage.hpp"
#include "lug_stdlib.h"
#include "lug_auxlib.h"
#include "lug_value.h"
#include "lug_filesys.h"

namespace Luggage::Lib {


// Library implementation functions -----------------------------

static Value Write(VMCore& vm, Value* args, std::size_t argCount) {
    auto writer = vm.config.writeFn;
    
    for (std::size_t i = 0; i < argCount; i++) {
        if (i > 0) writer(" ");
        
        Value value = args[i];;
        ObjString* stringed = value.ToString(vm);

        // The user may define a null terminator in their string. Since std::string_view
        // uses strlen in their default constructor, we instead use the explicit constructor
        // that takes in the length of the string, and we'll pass THAT into the writer function
        std::string_view stringView{ stringed->characters, stringed->length };
        writer(stringView);
    }

    return Value::Nil();
}


static Value Print(VMCore& vm, Value* args, std::size_t argCount) {
    Write(vm, args, argCount);
    vm.config.writeFn("\n");

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
    throw vm.RuntimeError(errorMessage->characters);
    
    return 0;
}


static Value Assert(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 2);
    CheckType(vm, args[0], ValueType::BOOLEAN);
    CheckType(vm, args[1], ObjType::STRING);

    if (!args[0].AsBool()) {
        ObjString* errorMessage = args[1].AsObj<ObjString>();
        ObjString* assertionPrefix = ObjString::FromLiteral(vm, "assertion failed: ");
        ObjString* fullMessage = assertionPrefix->Concatenate(errorMessage);

        throw vm.RuntimeError(fullMessage->characters);
    }
    
    return 0;
}


static Value Pcall(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ObjType::CLOSURE);

    ObjClosure* function = args[0].AsObj<ObjClosure>();
    ObjThing* result = ObjThing::New(vm);
    vm.stack.Push(result);

    /*
        When we error, we want to rewind to the saved state of the
        stack. Keep the old state here and if fail, set stackTop and
        frameCount back to its originals.
    */
    Value* oldTop = vm.stack.top;
    std::size_t oldFrameCount = vm.stack.frameCount;

    try {
        vm.stack.Push(function);
        vm.Call(function, 0);
        Value returned = vm.stack.Pop();
        result->Set("success", true);
        result->Set("result", returned);

    } catch (LugError& error) {
        const char* errorMessage = error.message.c_str();
        
        result->Set("success", false);
        result->Set("result", ObjString::FromLiteral(vm, errorMessage));

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
        vm.stack.frameCount = oldFrameCount;
        vm.stack.top = oldTop;

    }

    return vm.stack.Pop();
}


static Value Type(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    
    // Value::StringType() returns a literal.
    const char* stringType = args[0].StringType();
    return ObjString::FromLiteral(vm, stringType);
}


static Value Unbox(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ObjType::STRING);

    ObjString* filePath = args[0].AsObj<ObjString>();
    auto resolved = Filesystem::ResolveModulePath(vm.stack.CurrentModulePath(), filePath->characters);
    if (!resolved) {
        throw vm.RuntimeError("could not find script '%s'", filePath->characters);
    }
    
    auto source = Filesystem::ReadModule(*resolved);
    if (!source) {
        throw vm.RuntimeError("could not read script '%s'", filePath->characters);
    }

    ObjModule* module = vm.LoadModule(vm.stack.CurrentModulePath(), *resolved);
    ObjClosure* closure = ObjClosure::New(vm, module->function, module);
    vm.Call(closure, 0);

    return vm.stack.Peek(0);
}


// Library out ------------------------------

static constexpr NativeEntry baseLib[] = {
    { "write",      &Write },
    { "print",      &Print },
    { "clock",      &Clock },
    { "error",      &Error },
    { "assert",     &Assert },
    { "pcall",      &Pcall },
    { "type",       &Type },
    { "unbox",      &Unbox },
};

// Function declaration of defined in lug_stdlib.h
void OpenBase(VMCore& vm) { 
    DefineLib(vm, nullptr, baseLib); 
}

}