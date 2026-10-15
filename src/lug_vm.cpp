#include <cstdio>
#include <cstring>

#include "lug_vm.h"
#include "lug_opcodes.h"
#include "lug_debug.h"
#include "lug_compiler.h"
#include "lug_memory.h"

#include "lug_stdlib.h"

namespace Luggage {

// Helper functions -----------------------------------

/**
 * A value is falsy if it's explicitly set as 'nil' or 'false'.
 * Anything else is truthy.
 */
static bool IsFalsy(Value value) {
    return value.IsNil() || value.IsBool() && !value.AsBool();
}


static bool IsTruthy(Value value) {
    return !IsFalsy(value);
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
    frameCount = 0;
}


/**
 * Clamps a list index given the list's number of items.
 * If it's negative, it will wrap around to the end of the list.
 * If the index is larger than the number of items, we'll throw
 * an error.
 * Otherwise, just keep the list index.
 */
int VMCore::ClampIndex(int index, int itemCount) {
    // This handles the wrap-around case for negative numbers.
    if (index < 0) {
        return ((index % itemCount) + itemCount) % itemCount; 
    }

    // -1 because itemCount starts at 1, while indices start at 0
    if (index > itemCount - 1) {
        RuntimeError("Index '%d' out of range.", index);
    }

    return index;
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
 * Defines a single native function as a global with 
 * the given name.
 */
void VMCore::DefineNative(const char* name, NativeFn function) {
    Push(ObjString::CopyFrom(*this, name, std::strlen(name)));
    Push(ObjNative::New(*this, name, function));

    globals.Set(Peek(1).AsObj<ObjString>(), Peek(0));

    Pop();
    Pop();
}


/**
 * Adds an object to the VM's object linked list.
 */
void VMCore::AddObject(Obj* object) {
    object->next = objects;
    objects = object;
}


void VMCore::CallValue(Value value, std::uint8_t argCount) {
    if (!value.IsObj()) {
        RuntimeError("Can't call values of type '%s'.", value.StringType());
    }

    switch (value.AsObj()->type) {
        case ObjType::CLOSURE: {
            return Call(value.AsObj<ObjClosure>(), argCount);
        }

        case ObjType::NATIVE: {
            ObjNative* native = value.AsObj<ObjNative>();
            Value result = native->function(*this, stackTop - argCount, argCount);

            // Rewind the stack right before the ObjNative.
            stackTop -= argCount + 1;

            // Push the result of the function.
            Push(result);
            break;
        }

        default: {
            RuntimeError("Can only call functions.");
        }
    }
}


void VMCore::Call(ObjClosure* closure, std::uint8_t argCount) {
    if (argCount != closure->function->arity) {
        RuntimeError("Expected %d arguments but got %d.", closure->function->arity, argCount);
    }

    if (frameCount == FRAMES_MAX) {
        RuntimeError("Stack overflow.");
    }

    CallFrame& frame = frames[frameCount++];
    frame.closure = closure;
    frame.ip = closure->function->chunk.bytecode;
    frame.slots = stackTop - argCount - 1;
}


ObjUpvalue* VMCore::CaptureUpvalue(Value* local) {
    ObjUpvalue* previousUpvalue = nullptr;
    ObjUpvalue* upvalue = openUpvalues;

    while (upvalue != nullptr && upvalue->location > local) {
        previousUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != nullptr && upvalue->location == local) {
        return upvalue;
    }

    ObjUpvalue* createdUpvalue = ObjUpvalue::New(*this, local);
    createdUpvalue->next = upvalue;
    if (previousUpvalue == nullptr) {
        openUpvalues = createdUpvalue;
    } else {
        previousUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}


void VMCore::CloseUpvalues(Value* last) {
    while (openUpvalues != nullptr && openUpvalues->location >= last) {
        ObjUpvalue* upvalue = openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        openUpvalues = upvalue->next;
    }
}


/**
 * The actual interpreter loop where it runs 
 * each instruction from its chunk.
 */
void VMCore::Run() {
    CallFrame* frame = &frames[frameCount - 1];

    #define READ_BYTE() (*frame->ip++)
    #define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_BYTE()])
    #define READ_STRING() (READ_CONSTANT().AsObj<ObjString>())
    #define READ_SHORT() ((READ_BYTE() << 8) | READ_BYTE())

    #define BINARY_OP(op)                               \
    do {                                                \
        Value b = Pop();                                \
        Value a = Pop();                                \
        if (!a.IsNumber() || !b.IsNumber()) {           \
            RuntimeError("Operands must be numbers.");  \
        }                                               \
        Push(a.AsNumber() op b.AsNumber());             \
    } while (false)

    #define CLAMP_INDEX(index, numItems)                                    \
        if ((index) >= static_cast<int>(numItems)) {                        \
            RuntimeError("Index out of range.");                            \
        }                                                                   \
                                                                            \
        if ((index) < 0) {                                                  \
            index = ((index) % (numItems) + (numItems)) % (numItems);       \
        }
        

    // Dispatch table shenanigans -------------------

#ifdef LUG_DEBUG_TRACE_EXECUTION
    #define TRACE_EXECUTION()                                               \
    do {                                                                    \
        std::printf("          ");                                          \
        for (Value* slot = stack; slot < stackTop; slot++) {                \
            std::printf("[ ");                                              \
            Value::Print(*slot);                                            \
            std::printf(" ]");                                              \
        }                                                                   \
                                                                            \
        std::printf("\n");                                                  \
                                                                            \
        Debug::DisassembleInstruction(                                      \
            frame->closure->function->chunk,                                \
            (int)(frame->ip - frame->closure->function->chunk.bytecode)     \
        );                                                                  \
    } while (false)

#else
    #define TRACE_EXECUTION() do { } while (false)
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
                RuntimeError("Can't add operands of type '%s' and '%s' together.", 
                    Peek(1).StringType(), 
                    Peek(0).StringType()
                );
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
            if (Peek(0).IsNumber() && Peek(0).AsNumber() == 0) {
                RuntimeError("Can't divide by zero.");
            }

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

        CASE(JUMP): {
            std::uint16_t numBytesToJump = READ_SHORT();
            frame->ip += numBytesToJump;
            DISPATCH();
        }

        CASE(JUMP_IF_FALSE): {
            std::uint16_t numBytesToJump = READ_SHORT();
            if (IsFalsy(Peek(0))) {
                frame->ip += numBytesToJump;
            }
            DISPATCH();
        }

        CASE(JUMP_IF_TRUE): {
            std::uint16_t numBytesToJump = READ_SHORT();
            if (IsTruthy(Peek(0))) {
                frame->ip += numBytesToJump;
            }
            DISPATCH();
        }

        CASE(JUMP_IF_FALSE_OR_POP): {
            std::uint16_t numBytesToJump = READ_SHORT();
            if (IsFalsy(Peek(0))) {
                frame->ip += numBytesToJump;
            } else {
                Pop();
            }
            DISPATCH();
        }

        CASE(JUMP_IF_TRUE_OR_POP): {
            std::uint16_t numBytesToJump = READ_SHORT();
            if (IsTruthy(Peek(0))) {
                frame->ip += numBytesToJump;
            } else {
                Pop();
            }
            DISPATCH();
        }

        CASE(POP_JUMP_IF_FALSE): {
            std::uint16_t numBytesToJump = READ_SHORT();
            if (IsFalsy(Pop())) {
                frame->ip += numBytesToJump;
            }

            DISPATCH();
        }

        CASE(POP_JUMP_IF_TRUE): {
            std::uint16_t numBytesToJump = READ_SHORT();
            if (IsTruthy(Pop())) {
                frame->ip += numBytesToJump;
            }

            DISPATCH();
        }

        CASE(LOOP): {
            std::uint16_t numBytesToJumpBack = READ_SHORT();
            frame->ip -= numBytesToJumpBack;
            DISPATCH();
        }

        CASE(COUNT_PREP): {
            std::uint16_t jumpToEnd = READ_SHORT();

            Value start = Peek(2);
            Value stop = Peek(1);
            Value step = Peek(0);

            if (!start.IsNumber()) RuntimeError("'count' initial value must be a number.");
            if (!stop.IsNumber()) RuntimeError("'count' limit value must be a number.");
            if (!step.IsNumber()) RuntimeError("'count' step value must be a number.");
            
            // Check if the loop would even run at all.
            if (step.AsNumber() == 0) {
                RuntimeError("'count' step value cannot be zero.");
            }
            
            // This handles cases like:
            // count a from 0 to 10 by -1 do ... end
            // count b from 10 to 0 do ... end
            // where the loop would never run because the step is in the wrong direction.
            if (
                step.AsNumber() > 0 ? 
                start.AsNumber() > step.AsNumber() :
                start.AsNumber() < step.AsNumber()
            ) {
                frame->ip += jumpToEnd;
            }

            DISPATCH();
        }

        CASE(COUNT_LOOP): {
            /* 
                When COUNT_LOOP is reached, the stack will look like this:
                [ 00000 ][ 00005 ][ 00001 ]
                  ^^^^^    ^^^^^    ^^^^^    ^^^^^
                  start    limit     step    stackTop

                Where Peek(0) = step, Peek(1) = limit, and Peek(2) = start.
            */

            std::uint16_t jumpToStart = READ_SHORT();

            ASSERT(Peek(0).IsNumber(), "Invalid step");
            ASSERT(Peek(1).IsNumber(), "Invalid limit");

            if (!Peek(2).IsNumber()) {
                RuntimeError("'count' step must be a number.");
            }

            // Increment index.
            stackTop[-3] = Value(stackTop[-3].AsNumber() + stackTop[-1].AsNumber());
            if (Peek(0).AsNumber() > 0 ? 
                Peek(2).AsNumber() <= Peek(1).AsNumber() :
                Peek(2).AsNumber() >= Peek(1).AsNumber()
            ) {
                // Repeat loop
                frame->ip -= jumpToStart;
            }

            DISPATCH();
        }

        CASE(PLACEHOLDER): {
            ASSERT(false, "Placeholder found during runtime.");
        }
        
        CASE(POP): {
            Pop();
            DISPATCH();
        }

        CASE(DUPLICATE): {
            Push(Peek(0));
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
                RuntimeError("Undefined variable '%s'.", name->characters);
            }

            DISPATCH();
        }

        CASE(GET_LOCAL): {
            std::uint8_t slot = READ_BYTE();
            Push(frame->slots[slot]);

            DISPATCH();
        }

        CASE(SET_LOCAL): {
            std::uint8_t slot = READ_BYTE();
            frame->slots[slot] = Peek(0);
            
            DISPATCH();
        }

        CASE(GET_UPVALUE): {
            std::uint8_t slot = READ_BYTE();
            Push(*frame->closure->upvalues[slot]->location);

            DISPATCH();
        }

        CASE(SET_UPVALUE): {
            std::uint8_t slot = READ_BYTE();
            *frame->closure->upvalues[slot]->location = Peek(0);

            DISPATCH();
        }

        CASE(CLOSE_UPVALUE): {
            CloseUpvalues(stackTop - 1);
            Pop();

            DISPATCH();
        }
        
        CASE(CALL): {
            std::uint8_t argCount = READ_BYTE();
            
            // Attempt to call the callee, whether it is of any value type.
            CallValue(Peek(argCount), argCount);
            
            // After we call the value, there will be a new call frame on the 
            // frame stack. Let's update our pointer to match that.
            frame = &frames[frameCount - 1];
            
            DISPATCH();
        }

        CASE(CLOSURE): {
            ObjFunction* function = READ_CONSTANT().AsObj<ObjFunction>();
            ObjClosure* closure = ObjClosure::New(*this, function);
            Push(closure);

            for (std::size_t i = 0; i < closure->upvalueCount; i++) {
                std::uint8_t isLocal = READ_BYTE();
                std::uint8_t index = READ_BYTE();
                
                // When we emitted OP_CLOSURE, we were done parsing
                // a function declaration. Now, the current function is
                // the SURROUNDING function of that declaration, so we can
                // read the upvalue right from the current call frame to grab
                // it from the enclosing function.
                if (isLocal) {
                    closure->upvalues[i] = CaptureUpvalue(frame->slots + index);
                } else {
                    closure->upvalues[i] = frame->closure->upvalues[index];
                }
            }

            DISPATCH();
        }
        
        CASE(LIST): {
            ObjList* newList = ObjList::New(*this);
            Push(newList);

            DISPATCH();
        }

        CASE(DICTIONARY): {
            ObjDictionary* newDictionary = ObjDictionary::New(*this);
            Push(newDictionary);

            DISPATCH();
        }

        CASE(APPEND): {
            ObjList* list = Peek(1).AsObj<ObjList>();
            list->Append(Pop());
            
            DISPATCH();
        }

        CASE(DEFINE_ENTRY): {
            ObjDictionary* dictionary = Peek(2).AsObj<ObjDictionary>();
            Value key = Peek(1);
            Value value = Peek(0);

            dictionary->Set(key, value);

            Pop();
            Pop();

            DISPATCH();
        }

        CASE(GET_INDEX): {
            //    Peek(1)     Peek(0)
            // [ collection ][ index ]
            //                 ^^^^^ stackTop. You are here.
            Value& collection = Peek(1);
            if (!collection.IsObj(ObjType::LIST) && !collection.IsObj(ObjType::DICTIONARY)) {
                RuntimeError("Value of type '%s' is not indexable.", Peek(2).StringType());
            }
            
            switch (collection.AsObj()->type) {
                case ObjType::LIST: {
                    if (!Peek(0).IsInteger()) RuntimeError("List index must be an integer.");
                    
                    ObjList* list = collection.AsObj<ObjList>();
                    int index = Pop().AsInteger();
                    index = ClampIndex(index, list->itemCount);
        
                    Value value = list->Get(index);
                    Push(value);
                }

                case ObjType::DICTIONARY: {
                    ObjDictionary* dictionary = collection.AsObj<ObjDictionary>();

                    Value key = Peek(0);
                    Value value = dictionary->Get(key);

                    Pop(); // Key
                    Pop(); // Collection
                    
                    Push(value);
                }
            }

            DISPATCH();
        }
        
        CASE(SET_INDEX): {
            //     Peek(2)    Peek(1)    Peek(0)
            // [ collection ][ index ][ newValue ]
            //                                    ^^^^^^ stackTop. You are here.
            Value& collection = Peek(2);
            if (!collection.IsObj(ObjType::LIST) && !collection.IsObj(ObjType::DICTIONARY)) {
                RuntimeError("Value of type '%s' is not indexable.", Peek(2).StringType());
            }

            Value newValue = Peek(0);

            switch (collection.AsObj()->type) {
                case ObjType::LIST: {
                    // Lists must always have integers as indices.
                    if (!Peek(1).IsInteger()) RuntimeError("List index must be an integer.");
                    
                    ObjList* list = collection.AsObj<ObjList>();

                    int index = Peek(1).AsInteger();
                    index = ClampIndex(index, list->itemCount);

                    list->Set(index, newValue); 
                    Pop(); // New value
                    Pop(); // Index
                    Pop(); // Collection

                    Push(newValue);
                }

                case ObjType::DICTIONARY: {
                    ObjDictionary* dictionary = collection.AsObj<ObjDictionary>();

                    Value key = Peek(1);
                    dictionary->Set(key, newValue);

                    Pop(); // New value
                    Pop(); // Index
                    Pop(); // Collectione

                    Push(newValue);
                }
            }


            


            DISPATCH();
        }

        CASE(GET_ATTRIBUTE): {
            ObjString* attributeName = READ_STRING();

            if (!Peek(0).IsObj(ObjType::THING)) {
                RuntimeError(
                    "Attribute '%s' does not exist on value of type '%s'.", 
                    attributeName->characters, 
                    Peek(0).StringType()
                );
            }

            ObjThing* thing = Peek(0).AsObj<ObjThing>();
            Value value = thing->Get(attributeName);
            Pop(); // Thing
            Push(value);

            DISPATCH();
        }


        CASE(SET_ATTRIBUTE): {
            ObjString* attributeName = READ_STRING();
            if (!Peek(1).IsObj(ObjType::THING)) {
                RuntimeError(
                    "Attribute '%s' does not exist on value of type '%s'.", 
                    attributeName->characters, 
                    Peek(0).StringType()
                );
            }

            ObjThing* thing = Peek(1).AsObj<ObjThing>();
            Value value = Peek(0);
            thing->Set(attributeName, value);

            Pop();
            Pop();
            Push(value); // Push the value back since assignments leave the value on the stack.

            DISPATCH();
        }
        
        CASE(PRINT): {
            Value::Print(Pop());
            std::printf("\n");
            
            DISPATCH();
        }

        CASE(RETURN): {
            Value result = Pop();
            CloseUpvalues(frame->slots);

            frameCount--;
            if (frameCount == 0) {
                Pop();
                return;
            }

            stackTop = frame->slots;
            Push(result);
            frame = &frames[frameCount - 1];

            DISPATCH(); // Break out of the interpreter loop.
        }
    }

    ASSERT(0, "Unknown opcode.");

    #undef READ_BYTE
    #undef READ_CONSTANT
    #undef READ_STRING

    #undef CLAMP_INDEX
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

    try {
        ObjFunction* function = compiler.Compile(source.data());

        core->Push(Value(function));
        ObjClosure* closure = ObjClosure::New(*core, function);
        core->Pop();
        core->Push(closure);
        core->Call(closure, 0);
        core->Run();

    } catch (LugError error) {
        core->lastError = error;
        
        //! Not the best way right now to handle errors. Ideally the user'd like the stack to
        // rewind to how it was, but this is a good temporary fix.
        core->ResetStack();
        return false;
    }

    return true;
}


const LugError& VM::LastError() const {
    return core->lastError;
}


constexpr LibInclude operator|(LibInclude a, LibInclude b) {
    return static_cast<LibInclude>(
        static_cast<unsigned int>(a) | static_cast<unsigned int>(b)
    );
}


constexpr bool operator&(LibInclude a, LibInclude b) {
    return static_cast<unsigned int>(a) & static_cast<unsigned int>(b);
}  


void VM::LoadLibrary(LibInclude libraries) {
    if (libraries & LibInclude::NONE) return;
    if (libraries & LibInclude::BASE) Lib::OpenBase(*core);
}

}