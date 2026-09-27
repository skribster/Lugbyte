#include <cstdio>

#include "lug_vm.h"
#include "lug_opcodes.h"
#include "lug_debug.h"
#include "lug_compiler.h"
#include "lug_memory.h"

namespace Luggage {

// Helper functions -----------------------------------

/**
 * A value is falsy if it's explicitly set as 'nil' or 'false'.
 * Anything else is truthy.
 */
static bool IsFalsy(Value value) {
    return value.IsNil() || value.IsBool() && !value.AsBool();
}


// VMCore methods -----------------------------------

VMCore::VMCore() 
: strings(*this) 
, globals(*this)
{
    ResetStack();
}

VMCore::~VMCore() {
    Memory::FreeObjects(*this); 
}   


/**
 * Pushes the given value onto the stack.
 */
void VMCore::Push(Value value) {
    *stackTop = value;
    stackTop++;   
}


Value& VMCore::Peek(std::size_t distance) const {
    return stackTop[-1 - distance];
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
 * Tries to concatenate the two most recent values onto 
 * the stack, assuming they are of type ObjString.
 * Then, pushes back the concatenated result.
 */
void VMCore::Concatenate() {
    ObjString* b = Pop().AsObj<ObjString>();
    ObjString* a = Pop().AsObj<ObjString>();

    std::size_t length = a->length + b->length;
    char* chars = Memory::Allocate<char>(*this, length + 1);
    std::memcpy(chars, a->characters, a->length);
    std::memcpy(chars + a->length, b->characters, b->length);
    chars[length] = '\0';

    ObjString* result = ObjString::TakeFrom(*this, chars, length);
    Push(result);
}


/**
 * The actual interpreter loop where it runs 
 * each instruction from its chunk.
 */
void VMCore::Run() {
    #define READ_BYTE() (*ip++)
    #define READ_CONSTANT() (chunk->constants.values[READ_BYTE()])
    #define READ_STRING() (READ_CONSTANT().AsObj<ObjString>())

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
            Push(IsFalsy(value));
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
            if (Peek(0).IsObj(ObjType::STRING) && Peek(1).IsObj(ObjType::STRING)) {
                Concatenate();
            } else if (Peek(0).IsNumber() && Peek(1).IsNumber()) {
                double b = Pop().AsNumber();
                double a = Pop().AsNumber();

                Push(a + b);

            } else {
                RuntimeError("Operands must be two numbers or two strings.");
                return;
            }

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

        CASE(DEFINE_GLOBAL): {
            /*
                The operand of DEFINE_GLOBAL is a
                string constant, or the name of the
                global. The value of the global
                is the value on top of the stack.
            */
            ObjString* string = READ_STRING();
            globals.Set(string, Peek(0));
            Pop();

            DISPATCH();
        }

        CASE(GET_GLOBAL): {
            ObjString* name = READ_STRING();
            Value* value = globals.Get(name);

            if (value == nullptr) {
                RuntimeError("Undefined variable '%s'.", name->characters);
            }

            Push(*value);
            DISPATCH();
        }
        
        CASE(SET_GLOBAL): {
            ObjString* name = READ_STRING();

            // We assign the name to the value at the top of the stack.
            Value& value = Peek(0);
            bool isNewKey = globals.Set(name, value);
            if (isNewKey) {
                globals.Delete(name);
            }

            DISPATCH();
        }

        CASE(POP): {
            Pop();
            DISPATCH();
        }

        CASE(PRINT): {
            Value::Print(Pop());
            std::printf("\n");
            
            DISPATCH();
        }

        CASE(RETURN): {
            return; // Break out of the interpreter loop.
        }
    }

    #undef READ_BYTE
    #undef READ_CONSTANT
    #undef READ_STRING

    #undef BINARY_OP

    #undef TRACE_EXECUTION 
    #undef DISPATCH
    #undef CASE
    #undef INTERPRET_LOOP
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