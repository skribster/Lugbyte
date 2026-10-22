#pragma once

#include <cstring>
#include "luggage.hpp"

#include "lug_common.h"
#include "lug_value.h"
#include "lug_object.h"
#include "lug_chunk.h"
#include "lug_table.h"

namespace Luggage {

constexpr std::size_t FRAMES_MAX = 64;
constexpr std::size_t STACK_MAX = (FRAMES_MAX * UINT8_COUNT);

struct CallFrame {
    ObjClosure* closure{ nullptr };
    std::uint8_t* ip{ nullptr };
    Value* slots{ nullptr };
};


class VMCore {
    public:
    VMCore();
    ~VMCore();
    
    void DefineNative(const char* name, NativeFn function);
    void AddObject(Obj* obj);
    
    void Push(Value value);
    Value& Peek(std::size_t distance) const;
    Value Pop();
    
    void CallValue(Value value, std::uint8_t argCount, std::uint8_t shared);
    void Call(ObjClosure* closure, std::uint8_t argCount, std::uint8_t shared = 0);
    ObjUpvalue* CaptureUpvalue(Value* local);
    void CloseUpvalues(Value* last);
    
    void ResetStack();
    ObjThing* GetProtoOf(Value value) const;
    ObjString* ToString(Value value);
    Value Deepcopy(const Value& value);

    Table strings;
    Table globals;
    Obj* objects{ nullptr };

    CallFrame frames[FRAMES_MAX];
    std::size_t frameCount{ 0 };
    Value stack[STACK_MAX];
    Value* stackTop{ nullptr };
    ObjUpvalue* openUpvalues{ nullptr };
    
    template<typename... Args>
    [[noreturn]] void RuntimeError(const char* format, Args... args);

    std::size_t bytesAllocated{ 0 };
    
private:
    void Concatenate();
    inline int ClampIndex(int index, int numItems);
    void Run(std::size_t stopDepth);
    
    LugError lastError;

    friend class VM;
};


/**
 * Clamps a list index given the list's number of items.
 * If it's negative, it will wrap around to the end of the list.
 * If the index is larger than the number of items, we'll throw
 * an error.
 * Otherwise, just keep the list index.
 */
inline int VMCore::ClampIndex(int index, int numItems) {
    // This handles the wrap-around case for negative numbers.
    if (index < 0) {
        return ((index % numItems) + numItems) % numItems; 
    }

    // -1 because itemCount starts at 1, while indices start at 0
    if (index > numItems - 1) {
        RuntimeError("Index '%d' out of range.", index);
    }

    return index;
}


template<typename... Args>
void VMCore::RuntimeError(const char* message, Args... args) {
    // Compute required length for the std::string
    int length = std::snprintf(nullptr, 0, message, args...);
    ASSERT(length, "snprintf formatting error");
    // Allocate exactly enough space
    std::string formatted(length, '\0');
    // Format the error message with the args
    std::snprintf(formatted.data(), static_cast<std::size_t>(length) + 1, message, args...);

    std::string errorMessage;
    errorMessage += formatted;
    errorMessage += '\n';

    for (int i = frameCount - 1; i >= 0; i--) {
        CallFrame& frame = frames[i];
        ObjFunction* function = frame.closure->function;
        std::size_t instruction = frame.ip - function->chunk.bytecode.Data() - 1;
        std::size_t line = function->chunk.GetLine(instruction);

        errorMessage += "[line ";
        errorMessage += std::to_string(line);
        errorMessage += "] in ";

        if (function->name == nullptr) {
            errorMessage += "script\n";
        } else {
            errorMessage.append(function->name->characters);
            errorMessage += "()\n";
        }
    }

    LugError error;
    error.type = ErrorType::RUNTIME;
    error.message = errorMessage;

    ObjString* messageValue = ObjString::FromSource(
        *this, errorMessage.c_str(), 
        std::strlen(errorMessage.c_str())
    );

    Push(messageValue);

    throw error;
}


}