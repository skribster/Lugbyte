#include <cstdio>

#include "lug_vm.h"
#include "lug_opcodes.h"
#include "lug_debug.h"
#include "lug_compiler.h"

namespace Luggage {

// Helper functions -----------------------------------

/**
 * A value is falsy if it's explicitly set as 'nil' or 'false'.
 * Anything else is truthy.
 */
static bool isFalsy(Value value) {
    return value.IsNil() || value.IsBool() && !value.AsBool();
}


// VMCore methods -----------------------------------

VMCore::VMCore() {
    ResetStack();
}

/**
 * Pushes the given value onto the stack.
 */
void VMCore::Push(Value value) {
    *stackTop = value;
    stackTop++;   
}

/**
 * Pops the top value off the stack and returns
 * it back.
 */
Value VMCore::Pop() {
    stackTop--;
    return *stackTop;
}

/**
 * Discards all of the stack's items.
 */
void VMCore::ResetStack() {
    stackTop = stack;
}

/**
 * The actual interpreter loop where it runs 
 * each instruction from its chunk.
 */
void VMCore::Run() {
    #define READ_BYTE() (*ip++)
    #define READ_CONSTANT() (chunk->constants.values[READ_BYTE()])

    #define BINARY_OP(op)                               \
    do {                                                \
        Value b = Pop();                                \
        Value a = Pop();                                \
        if (!a.IsNumber() || !b.IsNumber()) {           \
            RuntimeError("Operands must be numbers.");  \
        }                                               \
        Push(a.AsNumber() op b.AsNumber());             \
    } while (false)
        

    // Dispatch table shenanigans -------------------

#ifdef LUG_DEBUG_TRACE_EXECUTION
    #define TRACE_EXECUTION()                                   \
    do {                                                        \
        std::printf("          ");                              \
        for (Value* slot = stack; slot < stackTop; slot++) {    \
            std::printf("[ ");                                  \
            Value::Print(*slot);                                \
            std::printf(" ]");                                  \
        }                                                       \
                                                                \
        std::printf("\n");                                      \
                                                                \
        Debug::DisassembleInstruction(                          \
            *chunk, (int)(ip - chunk->bytecode)                 \
        );                                                      \
    } while (false);

#else
    #define TRACE_EXECUTION do { } while (false);
#endif

#ifdef USE_COMPUTED_GOTO
    static void* dispatchTable[] = {
        #define OPCODE(name) &&CASE_##name,
        OPCODE_LIST
        #undef OPCODE
    };

    #define DISPATCH() do {                 \
        TRACE_EXECUTION();                  \
        goto *dispatchTable[READ_BYTE()];   \
    } while (false); 

    #define CASE(name) CASE_##name
    #define INTERPRET_LOOP DISPATCH();
                       
#else
    #define DISPATCH() goto loop
    #define CASE(name) case OpCode::name
    #define INTERPRET_LOOP                                                  \
        OpCode::Type instruction;                                           \
        loop:                                                               \
            TRACE_EXECUTION();                                              \
            switch (instruction = static_cast<OpCode::Type>(READ_BYTE()))

#endif

    // Dispatch loop!!! -------------------
    INTERPRET_LOOP
    {
        CASE(CONSTANT): {
            Value constant = READ_CONSTANT();
            Push(constant);
            DISPATCH();
        }

        CASE(NEGATE): {
            Value value = Pop();
            if (!value.IsNumber()) {
                RuntimeError("Operand must be a number.");
            }

            Push(-value.AsNumber());
            DISPATCH();
        }

        CASE(NOT): {
            Value value = Pop();
            Push(isFalsy(value));
            DISPATCH();
        }

        CASE(TRUE): {
            Push(true);
            DISPATCH();
        }

        CASE(FALSE): {
            Push(false);
            DISPATCH();
        }

        CASE(NIL): {
            Push(Value::Nil());
            DISPATCH();
        }

        CASE(ADD): {
            BINARY_OP(+);
            DISPATCH();
        }

        CASE(SUBTRACT): {
            BINARY_OP(-);
            DISPATCH();
        }

        CASE(MULTIPLY): {
            BINARY_OP(*);
            DISPATCH();
        }

        CASE(DIVIDE): {
            BINARY_OP(/);
            DISPATCH();
        }

        CASE(EQUAL): {
            Value b = Pop();
            Value a = Pop();
            Push(a == b);
            DISPATCH();
        }

        CASE(GREATER): {
            BINARY_OP(>);
            DISPATCH();
        }
        
        CASE(GREATER_EQUAL): {
            BINARY_OP(>=);
            DISPATCH();
        }
        
        CASE(LESS): {
            BINARY_OP(<);
            DISPATCH();
        }
        
        CASE(LESS_EQUAL): {
            BINARY_OP(<=);
            DISPATCH();
        }

        CASE(RETURN): {
            Value::Print(Pop());
            std::printf("\n");
            return; // Break out of the interpreter loop.
        }
    }

    #undef READ_BYTE
    #undef READ_CONSTANT
    #undef BINARY_OP

    #undef TRACE_EXECUTION 
    #undef DISPATCH
    #undef CASE
    #undef INTERPRET_LOOP
}


void VMCore::RuntimeError(std::string message) {
    // Get current byte
    std::size_t byteOffset = static_cast<std::size_t>(ip - chunk->bytecode);
    std::size_t line = chunk->GetLine(byteOffset);

    std::string errorMessage;
    errorMessage += "[line ";
    errorMessage += std::to_string(line);
    errorMessage += "]\n";

    errorMessage += message;

    LugError error;
    error.type = ErrorType::RUNTIME;
    error.message = errorMessage;

    throw error;
}


// Luggage public header methods -----------------------------------

VM::VM() 
: core(std::make_unique<VMCore>()) 
{
    
}


VM::~VM() = default;


/**
 * Interprets a chunk's bytecode and returns
 * the result back.
 */
bool VM::Interpret(std::string_view source) {
    Compiler compiler{ *core };
    Chunk chunk{ *core };

    try {
        compiler.Compile(chunk, source.data());

        core->chunk = &chunk;
        core->ip = chunk.bytecode;
        core->Run();

    } catch (LugError error) {
        core->lastError = error;
        return false;
    }

    return true;
}


const LugError& VM::LastError() const {
    return core->lastError;
}


}