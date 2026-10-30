#pragma once

#include <cstring>
#include "luggage.hpp"

#include "lug_common.h"
#include "lug_value.h"
#include "lug_object.h"
#include "lug_chunk.h"
#include "lug_table.h"
#include "lug_compiler.h"
#include "lug_gc.h"

namespace Luggage {


struct CallFrame {
    ObjClosure* closure{ nullptr };
    std::uint8_t* ip{ nullptr };
    Value* slots{ nullptr };
};


struct Stack {
    static constexpr std::size_t FRAMES_MAX = 64;
    static constexpr std::size_t STACK_MAX = (FRAMES_MAX * UINT8_COUNT);

    CallFrame frames[FRAMES_MAX];
    std::size_t frameCount{ 0 };
    Value stack[STACK_MAX];
    Value* top{ nullptr };

    inline void AddFrame(ObjClosure* closure, Value* slots) {
        CallFrame& newFrame = frames[frameCount++];
        newFrame.closure = closure;
        newFrame.ip = closure->function->chunk.bytecode.Data();
        newFrame.slots = slots;
    }

    inline CallFrame* GetFrame() {
        return &frames[frameCount - 1];
    }

    inline std::filesystem::path CurrentModulePath() {
        ObjString* sourcePath = GetFrame()->closure->module->path;
        return std::filesystem::path(sourcePath->characters);
    }

    inline void RemoveFrame() {
        top = GetFrame()->slots;
        frameCount--;
    }

    inline void Reset() {
        top = stack;
        frameCount = 0;
    }
    
    inline void Print() {
        for (Value* slot = stack; slot < top; slot++) {                        \
            std::printf("[ ");                                                      \
            Value::Print(*slot);                                                    \
            std::printf(" ]");                                                      \
        }                                                                           \
                                                                                    \
        std::printf("\n");  
    }

    inline void Push(Value value) {
        *top = value;
        top++;
    }

    inline Value& Peek(std::size_t distance) const {
        return top[-1 - distance];
    }

    inline Value Pop() {
        top--;
        return *top;
    }
};


class VMCore {
public:
    VMCore(VMConfig config);
  
    InterpretResult Interpret(std::string_view source, std::filesystem::path sourcePath);
    void DefineNative(const char* name, NativeFn function);
    ObjModule* LoadModule(std::filesystem::path importerPath, std::filesystem::path modulePath);
    
    // Garbage collector methods ----------
    void AddObject(Obj* obj);
    void CollectGarbage();
    void MarkRoots();
    
    void PushFrame(ObjClosure* closure, std::uint8_t argCount, std::uint8_t shared);
    bool CallValue(Value value, std::uint8_t argCount, std::uint8_t shared);
    void Call(ObjClosure* closure, std::uint8_t argCount, std::uint8_t shared = 0);
    ObjUpvalue* CaptureUpvalue(Value* local);
    void CloseUpvalues(Value* last);
    
    ObjThing* GetProtoOf(Value value) const;
    Value Deepcopy(const Value& value);
    
    std::string StackTraceback();
    void PushError(LugError error);
    template<typename... Args>
    LugError RuntimeError(const char* format, Args... args);
    
    // Member variables -------------------
    Compiler compiler;
    GC gc;

    Table strings;
    Table builtins;
    Table modules;
    Stack stack;

    ObjUpvalue* openUpvalues{ nullptr };
    ObjModule* replModule{ nullptr };
    VMConfig config;
    
private:
    void Concatenate();
    void Run();
    
    LugError lastError;
    LugError memoryError;

    friend class VM;
};


template<typename... Args>
LugError VMCore::RuntimeError(const char* message, Args... args) {
    // Compute required length for the std::string
    int length = std::snprintf(nullptr, 0, message, args...);
    ASSERT(length, "snprintf formatting error");
    // Allocate exactly enough space
    std::string formatted(length, '\0');
    // Format the error message with the args
    std::snprintf(formatted.data(), static_cast<std::size_t>(length) + 1, message, args...);

    std::string errorMessage;
    errorMessage += "runtime error: ";
    errorMessage += formatted;
    errorMessage += '\n';
    errorMessage += StackTraceback();

    LugError error;
    error.type = InterpretResult::RUNTIME_ERROR;
    error.message = errorMessage;

    ObjString* messageValue = ObjString::FromSource(
        *this, errorMessage.c_str(), 
        std::strlen(errorMessage.c_str())
    );

    stack.Push(messageValue);

    return error;
}


}