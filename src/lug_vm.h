#pragma once

#include "luggage.hpp"

#include "lug_common.h"
#include "lug_value.h"
#include "lug_chunk.h"
#include "lug_table.h"

namespace Luggage {

constexpr std::size_t FRAMES_MAX = 64;
constexpr std::size_t STACK_MAX = (FRAMES_MAX * UINT8_COUNT);

struct CallFrame {
    ObjFunction* function{ nullptr };
    std::uint8_t* ip{ nullptr };
    Value* slots{ nullptr };
};


class VMCore {
    public:
    VMCore();
    ~VMCore();
    
    void DefineNative(const char* name, NativeFn function);
    void AddObject(Obj* obj);
    
    Table& GetStrings() { return strings; }
    Obj* GetObjects() { return objects; }
    
    template<typename... Args>
    [[noreturn]] void RuntimeError(const char* format, Args... args);

    std::size_t bytesAllocated{ 0 };
    
private:
    void ResetStack();
    void Concatenate();
    void Push(Value value);
    Value& Peek(std::size_t distance) const;
    Value Pop();
    
    void CallValue(Value value, std::uint8_t argCount);
    void Call(ObjFunction* function, std::uint8_t argCount);
    void Run();
    
    CallFrame frames[FRAMES_MAX];
    std::size_t frameCount{ 0 };
    Value stack[STACK_MAX];
    Value* stackTop{ nullptr };
    
    Table strings;
    Table globals;
    Obj* objects{ nullptr };
    
    LugError lastError;

    friend class VM;
};

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
        ObjFunction* function = frame.function;
        std::size_t instruction = frame.ip - function->chunk.bytecode - 1;
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

    throw error;
}


}