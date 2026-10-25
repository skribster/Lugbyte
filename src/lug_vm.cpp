#include <cstdio>
#include <cstring>
#include <cmath>
#include <unordered_map>

#include "lug_vm.h"
#include "lug_opcodes.h"
#include "lug_debug.h"
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


/**
 * Counts the trailing zeroes in a std::uint8_t byte
 * before the first set-bit.
 */
static int CountTrailingZeroes(std::uint8_t byte) {
    if (byte == 0) return 8;

    int index = 0;
    while (((byte >> index) & 1) == 0) index++;

    return index;
}


/**
 * The implementation for deepcopy. 
 */
static Value DeepcopyValue(VMCore& vm, const Value& value, std::unordered_map<Obj*, Obj*>& memo) {
    if (!value.IsObj()) return value;

    Obj* obj = value.AsObj();

    // Already copied (or currently being copied)? Reuse it.
    if (auto it = memo.find(obj); it != memo.end()) {
        return it->second;
    }

    switch (obj->type) {
        case ObjType::FUNCTION:
        case ObjType::CLOSURE:
        case ObjType::NATIVE:
            return value;

        // Strings are immutable.
        case ObjType::STRING: {
            return value;
        }

        case ObjType::LIST: {
            ObjList* original = value.AsObj<ObjList>();
            ObjList* copy = ObjList::New(vm);
            memo[obj] = copy;   // register BEFORE recursing

            for (auto& item : *original) {
                copy->Append(DeepcopyValue(vm, item, memo));
            }
            return copy;
        }

        case ObjType::DICT: {
            ObjDict* original = value.AsObj<ObjDict>();
            ObjDict* copy = ObjDict::New(vm);
            memo[obj] = copy;

            for (auto& entry : *original) {
                Value k = DeepcopyValue(vm, entry.key, memo);
                Value v = DeepcopyValue(vm, entry.value, memo);
                copy->Set(k, v);   // was original->Set
            }
            return copy;
        }

        case ObjType::THING: {
            ObjThing* original = value.AsObj<ObjThing>();
            ObjThing* copy = ObjThing::New(vm);
            memo[obj] = copy;

            for (auto& attribute : original->attributes) {
                Value name = DeepcopyValue(vm, attribute.key, memo);
                Value v = DeepcopyValue(vm, attribute.value, memo);

                copy->Set(name.AsObj<ObjString>(), v);
            }
            
            return copy;
        }

        default:
            return value;
    }
}


// VMCore methods -----------------------------------

VMCore::VMCore() 
    : strings(*this), globals(*this), gc(*this), compiler(*this)
{
    // We'll initialize the memory LugError at the start of the program
    // so that we don't have to allocate it later on.
    memoryError.type = ErrorType::MEMORY;
    memoryError.message = "Unable to allocate further memory.";

    stack.Reset();
}


VMCore::~VMCore() {
    gc.FreeObjects();
}   


bool VMCore::Interpret(std::string_view source) {
    try {
        ObjFunction* function = compiler.Compile(source.data());
        stack.Push(function);

        ObjClosure* closure = ObjClosure::New(*this, function);
        stack.Pop();
        stack.Push(closure);
        Call(closure, 0);

        // Pop the return value. During the REPL, the last OP_RETURN
        // started to push the 'nil' value onto the very bottom of the
        // stack, causing a bunch of 'nil' values to accumulate at the bottom. 
        stack.Pop();

    } catch (LugError& error) {
        lastError = error;
        
        //! Not the best way right now to handle errors. Ideally the user'd like the stack to
        // rewind to how it was, but this is a good temporary fix.
        stack.Reset();
        return false;

    } catch (std::bad_alloc& error) {
        lastError = memoryError;
        stack.Reset();

        return false;
    } 

    return true;
}


/**
 * Gets a value's proto, if there is any.
 */
ObjThing* VMCore::GetProtoOf(Value value) const {
    Value* proto = globals.Get(value.StringType());
    if (proto == nullptr || !proto->IsObj(ObjType::THING)) {
        return nullptr;
    }

    return proto->AsObj<ObjThing>();
}


/**
 * Returns back the stringified representation of a Luggage value.
 */
ObjString* VMCore::ToString(Value value) {
    if (value.IsObj()) {
        switch (value.AsObj()->type) {
            case ObjType::STRING: {
                return value.AsObj<ObjString>();
            }

            case ObjType::CLOSURE: {
                ObjClosure* closure = value.AsObj<ObjClosure>();
                ObjFunction* function = closure->function;

                if (function->name == nullptr) {
                    return ObjString::FromLiteral(*this, "<script>");
                }
                        
                return ObjString::FromFormat(*this, "<fn %s>", function->name->characters);
            }

            case ObjType::LIST: {
                ObjList* list = value.AsObj<ObjList>();
                return ObjString::FromFormat(*this, "<list %p>", static_cast<void*>(list));
            }

            case ObjType::DICT: {
                ObjDict* dict = value.AsObj<ObjDict>();
                return ObjString::FromFormat(*this, "<dict %p>", static_cast<void*>(dict));
            }

            case ObjType::THING: {
                ObjThing* thing = value.AsObj<ObjThing>();
                
                const Value* stringDunder = thing->Get("__tostring__");
                if (stringDunder != nullptr) {
                    stack.Push(*stringDunder);
                    stack.Push(thing);

                    // We will 'share' the thing.
                    CallValue(*stringDunder, 1, 0b1);
                    Value result = stack.Pop();
                    if (!result.IsObj(ObjType::STRING)) {
                        RuntimeError("'__tostring__' must return a string, returned '%s' instead", result.StringType());
                    }

                    return result.AsObj<ObjString>();
                }
                
                return ObjString::FromLiteral(*this, "<thing>");
            }
        }
    }

    switch (value.type) {
        case ValueType::BOOLEAN: {
            return ObjString::FromLiteral(*this, value.AsBool() ? "true" : "false");
        }

        case ValueType::NIL: {
            return ObjString::FromLiteral(*this, "nil");
        }

        case ValueType::NUMBER: {
            char buffer[20];
            std::size_t length = std::snprintf(buffer, sizeof(buffer), "%.14g", value.AsNumber());
            
            return ObjString::FromSource(*this, buffer, length);
        }
    }

    return nullptr;
}


/**
 * Returns a duplicate of a value that recursively copies
 * all underlying and nested elements.
 */
Value VMCore::Deepcopy(const Value& value) {
    std::unordered_map<Obj*, Obj*> memo;
    return DeepcopyValue(*this, value, memo);
}


/**
 * Tries to concatenate the two most recent values onto 
 * the stack, assuming they are of type ObjString.
 * Then, pushes back the concatenated result.
 */
void VMCore::Concatenate() {
    ObjString* b = stack.Peek(0).AsObj<ObjString>();
    ObjString* a = stack.Peek(1).AsObj<ObjString>();

    std::size_t length = a->length + b->length;
    char* chars = Memory::Allocate<char>(*this, length + 1);
    std::memcpy(chars, a->characters, a->length);
    std::memcpy(chars + a->length, b->characters, b->length);
    chars[length] = '\0';

    ObjString* result = ObjString::FromAllocated(*this, chars, length);
    stack.Pop();
    stack.Pop();
    stack.Push(result);
}


/**
 * Defines a single native function as a global with 
 * the given name.
 */
void VMCore::DefineNative(const char* name, NativeFn function) {
    stack.Push(ObjString::FromLiteral(*this, name));
    stack.Push(ObjNative::New(*this, name, function));

    globals.Set(stack.Peek(1).AsObj<ObjString>(), stack.Peek(0));

    stack.Pop();
    stack.Pop();
}


/**
 * Adds an object to the VM's object linked list.
 */
void VMCore::AddObject(Obj* obj) {
    gc.AddObject(obj);
}


/**
 * Frees all the VM's unused memory.
 */
void VMCore::CollectGarbage() {
    gc.Collect();
}


/**
 * This marks all of the values that the VM can reach directly
 * without going through a reference, otherwise known as the roots.
 */
void VMCore::MarkRoots() {
    /*
        The VM can access all the values on the stack. Let's loop 
        through the stack and mark any object.
    */
    for (Value* slot = stack.stack; slot < stack.top; slot++) {
        gc.MarkValue(*slot);
    }

    /*
        The VM can also access all of the global variables without
        going through a reference. Let's mark those as well.
    */
    gc.MarkTable(globals);

    /*
        Each call frame also has data that the VM can access.
        Each call frame has a closure that the VM uses for accessing
        upvalues, constants, etc. that may be of Obj type.

        Call frame -> Closure -> Chunk -> Constants -> other objects
        Call frame -> Closure -> Upvalues -> other objects
    */
    for (std::size_t i = 0; i < stack.frameCount; i++) {
        gc.MarkObject(stack.frames[i].closure);
    }

    /*
        The open upvalue list is another set of values that the
        VM can directly reach. Let's traverse the linked list and
        mark each one.
    */
    for (
        ObjUpvalue* upvalue = openUpvalues; 
        upvalue != nullptr; 
        upvalue = upvalue->next
    ) {
        gc.MarkObject(upvalue);
    }

    /*
        The compiler also grabs memory from the heap for literals
        and the constant table. We also want to keep track of those
        objects as well.
    */
    compiler.MarkRoots();
}


/**
 * This function will attempt to call any value of any type.
 */
void VMCore::CallValue(Value value, std::uint8_t argCount, std::uint8_t shared) {
    if (value.IsObj()) {
        switch (value.AsObj()->type) {
            case ObjType::CLOSURE: {
                Call(value.AsObj<ObjClosure>(), argCount, shared);
                return;
            }
            
            case ObjType::NATIVE: {               
                ObjNative* native = value.AsObj<ObjNative>();
                Value result = native->function(*this, stack.top - argCount, argCount);
                
                // Rewind the stack right before the ObjNative.
                stack.top -= argCount + 1;
                
                // Push the result of the function.
                stack.Push(result);
                return;
            }
            
            case ObjType::THING: {
                // Check if the 'thing' has a '__call__' dunder 
                // attached to it.
                ObjThing* thing = value.AsObj<ObjThing>();
                const Value* callDunder = thing->Get("__call__");
                if (callDunder == nullptr) break;
                
                CallValue(*callDunder, argCount, shared);
                return;
            }

            default: {
                ObjThing* valueProto = GetProtoOf(value);
                if (valueProto != nullptr) {
                    CallValue(valueProto, argCount, shared);
                    return;
                }
            }
        }
    }
    
    RuntimeError("Attempted to call a '%s' value.", value.StringType());
}


/**
 * This function will call a given callee and run the interpreter
 * until the function finishes.
 */
void VMCore::Call(ObjClosure* closure, std::uint8_t argCount, std::uint8_t shared) {
    ObjFunction::Arguments parameters = closure->function->parameters;
    if (argCount != parameters.arity) {
        RuntimeError("Expected %d arguments but got %d.", parameters.arity, argCount);
    }

    // If the 'shared' parameters don't align...
    std::uint8_t masked = parameters.shared & ~shared;
    if ((masked) != 0) {
        int index = CountTrailingZeroes(masked);
        RuntimeError("Expected a shared argument at argument %d", index + 1);
    }

    if (stack.frameCount == Stack::FRAMES_MAX) {
        RuntimeError("Stack overflow.");
    }

    stack.AddFrame(closure, stack.top - argCount - 1);
    Run();
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
    CallFrame* frame = stack.GetFrame();

    #define READ_BYTE() (*frame->ip++)
    #define READ_CONSTANT() (frame->closure->function->chunk.constants[READ_BYTE()])
    #define READ_STRING() (READ_CONSTANT().AsObj<ObjString>())
    #define READ_SHORT() ((READ_BYTE() << 8) | READ_BYTE())

    #define BINARY_OP(op)                               \
    do {                                                \
        Value b = stack.Pop();                                \
        Value a = stack.Pop();                                \
        if (!a.IsNumber() || !b.IsNumber()) {           \
            RuntimeError("Operands must be numbers.");  \
        }                                               \
        stack.Push(a.AsNumber() op b.AsNumber());             \
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
    #define TRACE_EXECUTION()                                                       \
    do {                                                                            \
        std::printf("          ");                                                  \
        stack.Print();                                                              \
                                                                                    \
        Debug::DisassembleInstruction(                                              \
            frame->closure->function->chunk,                                        \
            (int)(frame->ip - frame->closure->function->chunk.bytecode.Data())      \
        );                                                                          \
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
            stack.Push(constant);
            DISPATCH();
        }

        CASE(NEGATE): {
            Value value = stack.Pop();
            if (!value.IsNumber()) {
                RuntimeError("Operand must be a number.");
            }

            stack.Push(-value.AsNumber());
            DISPATCH();
        }

        CASE(NOT): {
            Value value = stack.Pop();
            stack.Push(IsFalsy(value));
            DISPATCH();
        }

        CASE(TRUE): {
            stack.Push(true);
            DISPATCH();
        }

        CASE(FALSE): {
            stack.Push(false);
            DISPATCH();
        }

        CASE(NIL): {
            stack.Push(Value::Nil());
            DISPATCH();
        }

        CASE(ADD): {
            if (stack.Peek(0).IsObj(ObjType::STRING) && stack.Peek(1).IsObj(ObjType::STRING)) {
                Concatenate();
            } else if (stack.Peek(0).IsNumber() && stack.Peek(1).IsNumber()) {
                double b = stack.Pop().AsNumber();
                double a = stack.Pop().AsNumber();

                stack.Push(a + b);

            } else {
                RuntimeError("Can't add operands of type '%s' and '%s' together.", 
                    stack.Peek(1).StringType(), 
                    stack.Peek(0).StringType()
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
            if (stack.Peek(0).IsNumber() && stack.Peek(0).AsNumber() == 0) {
                RuntimeError("Can't divide by zero.");
            }

            BINARY_OP(/);
            DISPATCH();
        }

        CASE(MODULO): {
            Value bValue = stack.Pop();
            Value aValue = stack.Pop();
            if (!aValue.IsNumber() || !bValue.IsNumber()) {           
                RuntimeError("Operands must be numbers.");
            }    

            double a = aValue.AsNumber();
            double b = bValue.AsNumber();
            stack.Push(a - std::floor(a / b) * b);
            
            DISPATCH();
        }

        CASE(EQUAL): {
            Value b = stack.Pop();
            Value a = stack.Pop();
            stack.Push(a == b);
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
            if (IsFalsy(stack.Peek(0))) {
                frame->ip += numBytesToJump;
            }
            DISPATCH();
        }

        CASE(JUMP_IF_TRUE): {
            std::uint16_t numBytesToJump = READ_SHORT();
            if (IsTruthy(stack.Peek(0))) {
                frame->ip += numBytesToJump;
            }
            DISPATCH();
        }

        CASE(JUMP_IF_FALSE_OR_POP): {
            std::uint16_t numBytesToJump = READ_SHORT();
            if (IsFalsy(stack.Peek(0))) {
                frame->ip += numBytesToJump;
            } else {
                stack.Pop();
            }
            DISPATCH();
        }

        CASE(JUMP_IF_TRUE_OR_POP): {
            std::uint16_t numBytesToJump = READ_SHORT();
            if (IsTruthy(stack.Peek(0))) {
                frame->ip += numBytesToJump;
            } else {
                stack.Pop();
            }
            DISPATCH();
        }

        CASE(POP_JUMP_IF_FALSE): {
            std::uint16_t numBytesToJump = READ_SHORT();
            if (IsFalsy(stack.Pop())) {
                frame->ip += numBytesToJump;
            }

            DISPATCH();
        }

        CASE(POP_JUMP_IF_TRUE): {
            std::uint16_t numBytesToJump = READ_SHORT();
            if (IsTruthy(stack.Pop())) {
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

            Value start = stack.Peek(2);
            Value stop = stack.Peek(1);
            Value step = stack.Peek(0);

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
                start.AsNumber() > stop.AsNumber() :
                start.AsNumber() < stop.AsNumber()
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

                ...where stack.Peek(0) = step, stack.Peek(1) = limit, and stack.Peek(2) = start.
            */

            std::uint16_t jumpToStart = READ_SHORT();

            Value& start = stack.Peek(2);
            Value& limit = stack.Peek(1);
            Value& step = stack.Peek(0);

            // COUNT_PREP should've already checked if these were numbers or not.
            // If we still have them as not a number, then we should assert.
            ASSERT(start.IsNumber(), "Invalid start");
            ASSERT(limit.IsNumber(), "Invalid limit");
            ASSERT(step.IsNumber(), "Invalid step");   

            // Increment index.
            start = start.AsNumber() + step.AsNumber();
            if (step.AsNumber() > 0 ? 
                start.AsNumber() <= limit.AsNumber() :
                start.AsNumber() >= limit.AsNumber()
            ) {
                // Repeat loop
                frame->ip -= jumpToStart;
            }

            DISPATCH();
        }

        CASE(FOREACH_PREP): {
            /*
                When FOREACH_PREP is reached, the stack may look like this:
                [  nil  ][  nil  ][  nil  ][   [1, 2, 3]   ]
                 ^^^^^^^  ^^^^^^^  ^^^^^^^     ^^^^^^^^^     ^^^^^
                probe idx  value  key/index    iterable     stackTop   

                ...where the probing index, key, and value values are nil.
            */

            std::uint16_t jumpToEnd = READ_SHORT();

            Value& value = stack.Peek(2);
            Value& key = stack.Peek(1);
            Value& iterable = stack.Peek(0);
            
            if (iterable.IsObj(ObjType::LIST)) {
                ObjList* list = iterable.AsObj<ObjList>();
                if (!key.IsNumber()) key = 0;

                // list->Get may return nullptr if the index is out of range.
                const Value* listValue = list->Get(key.AsNumber());
                ASSERT(listValue != nullptr, "The foreach index is out of range.");
                value = *listValue;
                
                if (static_cast<std::size_t>(key.AsInteger()) >= list->items.Count()) {
                    frame->ip += jumpToEnd;
                }
            } 
            
            else if (iterable.IsObj(ObjType::DICT)) {
                ObjDict* dict = iterable.AsObj<ObjDict>();
                Value& probingIndex = stack.Peek(3);
                if (!probingIndex.IsNumber()) probingIndex = 0;

                std::size_t index = static_cast<std::size_t>(probingIndex.AsInteger());
                while (
                    index <= dict->entries.capacity 
                    && dict->entries.entries[index].IsEmpty()
                ) index++;

                if (index == dict->entries.capacity) {
                    frame->ip += jumpToEnd;
                }

                Entry& currentEntry = dict->entries.entries[index];
                key = currentEntry.key;
                value = currentEntry.value;
                probingIndex = static_cast<int>(index);
            } 
            
            else {
                RuntimeError("'foreach' must iterate over an iterable type, got '%s' instead.", iterable.StringType());
            }

            DISPATCH();
        }

        CASE(FOREACH_LOOP): {
            /*
                When FOREACH_PREP is reached, the stack may look like this:
                [  nil  ][   1   ][   0   ][   [1, 2, 3]   ]
                 ^^^^^^^  ^^^^^^^  ^^^^^^^     ^^^^^^^^^     ^^^^^
                probe idx  value  key/index    iterable     stackTop   
            */

            std::uint16_t jumpToStart = READ_SHORT();

            Value& value = stack.Peek(2);
            Value& key = stack.Peek(1);
            Value& iterable = stack.Peek(0);

            if (iterable.IsObj(ObjType::LIST)) {
                ObjList* list = iterable.AsObj<ObjList>();
                key = key.AsInteger() + 1;

                // Check if we exceeded the index count. If so, that means
                // we're done with iterating through the list. Exit out of
                // the loop.
                if (key.AsInteger() < list->items.Count()) {
                    // In case we screwed something up by letting the index go out of
                    // range of the list, causing list->Get to return nullptr, we assert
                    const Value* listValue = list->Get(key.AsNumber());
                    ASSERT(listValue != nullptr, "The foreach index is out of range.");
                    value = *listValue;

                    frame->ip -= jumpToStart;
                }
            } 

            else if (iterable.IsObj(ObjType::DICT)) {
                ObjDict* dict = iterable.AsObj<ObjDict>();
                Value& probingIndex = stack.Peek(3);
                ASSERT(probingIndex.IsInteger(), "Invalid type for probing index");

                std::size_t index = static_cast<std::size_t>(probingIndex.AsInteger());
                do index++; while (
                    index <= dict->entries.capacity 
                    && dict->entries.entries[index].IsEmpty()
                );

                if (index < dict->entries.capacity) {
                    Entry& currentEntry = dict->entries.entries[index];
                    key = currentEntry.key;
                    value = currentEntry.value;
                    probingIndex = static_cast<int>(index);
                    frame->ip -= jumpToStart;
                }
            }
            
            else {
                ASSERT(false, "Invalid iterable");
            }

            DISPATCH();
        }

        CASE(PLACEHOLDER): {
            ASSERT(false, "Placeholder found during runtime.");
        }
        
        CASE(POP): {
            stack.Pop();
            DISPATCH();
        }

        CASE(DUPLICATE): {
            stack.Push(stack.Peek(0));
            DISPATCH();
        }

        CASE(COPY): {
            Value value = stack.Peek(0);
            Value copy = Deepcopy(value);
            stack.Pop();
            stack.Push(copy);

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
            globals.Set(string, stack.Peek(0));
            stack.Pop();

            DISPATCH();
        }

        CASE(GET_GLOBAL): {
            ObjString* name = READ_STRING();
            Value* value = globals.Get(name);

            if (value == nullptr) {
                RuntimeError("Undefined variable '%s'.", name->characters);
            }

            stack.Push(*value);
            DISPATCH();
        }
        
        CASE(SET_GLOBAL): {
            ObjString* name = READ_STRING();

            // We assign the name to the value at the top of the stack.
            Value& value = stack.Peek(0);
            bool isNewKey = globals.Set(name, value);
            
            if (isNewKey) {
                globals.Delete(name);
                RuntimeError("Undefined variable '%s'.", name->characters);
            }

            DISPATCH();
        }

        CASE(GET_LOCAL): {
            std::uint8_t slot = READ_BYTE();
            stack.Push(frame->slots[slot]);

            DISPATCH();
        }

        CASE(SET_LOCAL): {
            std::uint8_t slot = READ_BYTE();
            frame->slots[slot] = stack.Peek(0);
            
            DISPATCH();
        }

        CASE(GET_UPVALUE): {
            std::uint8_t slot = READ_BYTE();
            stack.Push(*frame->closure->upvalues[slot]->location);

            DISPATCH();
        }

        CASE(SET_UPVALUE): {
            std::uint8_t slot = READ_BYTE();
            *frame->closure->upvalues[slot]->location = stack.Peek(0);

            DISPATCH();
        }

        CASE(CLOSE_UPVALUE): {
            CloseUpvalues(stack.top - 1);
            stack.Pop();

            DISPATCH();
        }

        CASE(CALL): {
            std::uint8_t argCount = READ_BYTE();
            
            // Attempt to call the callee, whether it is of any value type.
            CallValue(stack.Peek(argCount), argCount, 0b0);
            
            DISPATCH();
        }
        
        CASE(CALL_SHARED): {
            std::uint8_t argCount = READ_BYTE();
            std::uint8_t shared = READ_BYTE();

            CallValue(stack.Peek(argCount), argCount, shared);
            
            DISPATCH();
        }

        CASE(SELF): {
            ObjString* methodName = READ_STRING();
            Value object = stack.Pop();
            ObjThing* thing = nullptr;

            if (object.IsObj(ObjType::THING)) {
                thing = object.AsObj<ObjThing>();

            } else if (GetProtoOf(object) != nullptr) {
                thing = GetProtoOf(object);

            } else {
                RuntimeError(
                    "Method '%s' does not exist in value of type '%s'.", 
                    methodName->characters, 
                    object.StringType()
                );
            }

            const Value* value = thing->Get(methodName);
            stack.Push(*value);
            stack.Push(object);

            DISPATCH();
        }

        CASE(CLOSURE): {
            ObjFunction* function = READ_CONSTANT().AsObj<ObjFunction>();
            ObjClosure* closure = ObjClosure::New(*this, function);
            stack.Push(closure);

            for (std::size_t i = 0; i < closure->upvalueCount; i++) {
                std::uint8_t isLocal = READ_BYTE();
                std::uint8_t index = READ_BYTE();
                
                // When we emitted OP_CLOSURE, we were done parsing
                // a function declaration. Now, the current function is
                // the SURROUNDING function of that declaration, so we can
                // read the upvalue right from the current ``call frame to grab
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
            stack.Push(newList);

            DISPATCH();
        }

        CASE(DICT): {
            ObjDict* newDict = ObjDict::New(*this);
            stack.Push(newDict);

            DISPATCH();
        }

        CASE(THING): {
            ObjThing* newThing = ObjThing::New(*this);
            stack.Push(newThing);

            DISPATCH();
        }

        CASE(CATEGORY): {
            ObjCategory* newCategory = ObjCategory::New(*this);
            stack.Push(newCategory);

            DISPATCH();
        }

        CASE(APPEND): {
            ObjList* list = stack.Peek(1).AsObj<ObjList>();
            list->Append(stack.Pop());
            
            DISPATCH();
        }

        CASE(DEFINE_ENTRY): {
            ObjDict* dictionary = stack.Peek(2).AsObj<ObjDict>();
            const Value& key = stack.Peek(1);
            const Value& value = stack.Peek(0);

            dictionary->Set(key, value);

            stack.Pop();
            stack.Pop();

            DISPATCH();
        }

        CASE(GET_INDEX): {
            //    stack.Peek(1)     stack.Peek(0)
            // [ collection ][ index ]
            //                 ^^^^^ stackTop. You are here.
            Value& collection = stack.Peek(1);
            if (!collection.IsObj(ObjType::LIST) && !collection.IsObj(ObjType::DICT)) {
                RuntimeError("Value of type '%s' is not indexable.", stack.Peek(2).StringType());
            }
            
            switch (collection.AsObj()->type) {
                case ObjType::LIST: {
                    if (!stack.Peek(0).IsInteger()) RuntimeError("List index must be an integer.");
                    
                    ObjList* list = collection.AsObj<ObjList>();
                    int index = stack.Peek(0).AsInteger();
                    const Value* value = list->Get(index);

                    stack.Pop(); // Index
                    stack.Pop(); // Collection

                    // Leave only the value of the indexed list.
                    stack.Push(*value);

                    break;
                }

                case ObjType::DICT: {
                    ObjDict* dict = collection.AsObj<ObjDict>();

                    Value& key = stack.Peek(0);
                    const Value* value = dict->Get(key);
                    if (value == nullptr) {
                        
                        RuntimeError("Could not find key in dictionary.");
                    }

                    stack.Pop(); // Key
                    stack.Pop(); // Collection
                    
                    stack.Push(*value);

                    break;
                }
            }

            DISPATCH();
        }
        
        CASE(SET_INDEX): {
            //     stack.Peek(2)    stack.Peek(1)    stack.Peek(0)
            // [ collection ][ index ][ newValue ]
            //                                    ^^^^^^ stackTop. You are here.
            Value& collection = stack.Peek(2);
            if (!collection.IsObj(ObjType::LIST) && !collection.IsObj(ObjType::DICT)) {
                RuntimeError("Value of type '%s' is not indexable.", stack.Peek(2).StringType());
            }

            Value newValue = stack.Peek(0);

            switch (collection.AsObj()->type) {
                case ObjType::LIST: {
                    // Lists must always have integers as indices.
                    if (!stack.Peek(1).IsInteger()) RuntimeError("List index must be an integer.");
                    
                    ObjList* list = collection.AsObj<ObjList>();
                    int index = stack.Peek(1).AsInteger();
                    list->Set(index, newValue); 

                    break;
                }

                case ObjType::DICT: {
                    ObjDict* dictionary = collection.AsObj<ObjDict>();

                    // Dictionaries have Lua semantics, where setting a key to nil
                    // removes the entry entirely.
                    // (i.e. a["hello"] <- nil removes the "hello" key as well, and
                    // won't be counted in loops.)

                    Value key = stack.Peek(1);
                    if (newValue.IsNil()) {
                        dictionary->Delete(key);
                    } else {
                        dictionary->Set(key, newValue);
                    }

                    break;
                }
            }

            stack.Pop(); // New value
            stack.Pop(); // Index
            stack.Pop(); // Collection

            stack.Push(newValue);

            DISPATCH();
        }

        CASE(GET_ATTRIBUTE): {
            ObjString* attributeName = READ_STRING();

            if (stack.Peek(0).IsObj(ObjType::CATEGORY)) {
                ObjCategory* category = stack.Peek(0).AsObj<ObjCategory>();
                const Value* type = category->GetType(attributeName);
                if (type == nullptr) {
                    RuntimeError("Type '%s' does not exist in category.", attributeName->characters);
                }

                stack.Pop(); // Category
                stack.Push(*type);

                DISPATCH();
            }

            if (stack.Peek(0).IsObj(ObjType::THING)) {
                ObjThing* thing = stack.Peek(0).AsObj<ObjThing>();
                const Value* value = thing->Get(attributeName);
                if (value == nullptr) {
                    RuntimeError("Thing has no attribute named '%s'.", attributeName->characters);
                }
                
                stack.Pop(); // Thing
                stack.Push(*value);

                DISPATCH();
            }

            ObjThing* proto = GetProtoOf(stack.Peek(0));
            if (proto && proto->Get(attributeName) != nullptr) {
                const Value* value = proto->Get(attributeName);
                if (value == nullptr) {
                    RuntimeError("Could not find '%s' in value.", attributeName->characters);
                }

                stack.Pop();
                stack.Push(*value);

                DISPATCH();
            }
            
            RuntimeError(
                "Type '%s' does not support attributes.", 
                stack.Peek(0).StringType()
            );

            DISPATCH();
        }

        CASE(SET_ATTRIBUTE): {
            // [ <thing> ][ 5 ]
            //   ^^^^^^^   ^^^   ^^^^^^
            //   object   value  stackTop
            //   stack.Peek(1)  stack.Peek(0)

            ObjString* attributeName = READ_STRING();
            Value object = stack.Peek(1);
            Value value = stack.Peek(0);

            if (object.IsObj(ObjType::THING)) {
                ObjThing* thing = object.AsObj<ObjThing>();
                if (value.IsNil()) {
                    thing->Delete(attributeName);
                } else {
                    thing->Set(attributeName, value);
                }

                stack.Pop();
                stack.Pop();
                stack.Push(value); // Push the value back since assignments leave the value on the stack.
                
                DISPATCH();
            }

            if (object.IsObj(ObjType::CATEGORY)) {
                ObjCategory* category = object.AsObj<ObjCategory>();

                // Note that we cannot add new kinds after the category literal.
                // category->AddType will error for us when we try and re-define
                // an attribute that has already been defined.
                category->AddType(attributeName, value);
                stack.Pop();

                DISPATCH();
            }

            RuntimeError(
                "Attribute '%s' does not exist in value of type '%s'.", 
                attributeName->characters, 
                stack.Peek(0).StringType()
            );
        }
        
        CASE(PRINT): {
            Value::Print(stack.Pop());
            std::printf("\n");
            
            DISPATCH();
        }

        CASE(RETURN): {
            Value result = stack.Pop();
            CloseUpvalues(frame->slots);

            stack.RemoveFrame();
            stack.Push(result);

            return;
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
    return core->Interpret(source);
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
    if (libraries & LibInclude::NONE)           return;
    if (libraries & LibInclude::BASE)           Lib::OpenBase(*core);
    if (libraries & LibInclude::MATH)           Lib::OpenMath(*core);
    if (libraries & LibInclude::STRING)         Lib::OpenString(*core);
    if (libraries & LibInclude::COLLECTION)     Lib::OpenCollection(*core);
    if (libraries & LibInclude::NUMBER)         Lib::OpenNumber(*core);
}

}