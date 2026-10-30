#include <cstdio>
#include <cstring>
#include <cmath>
#include <unordered_map>

#include "lug_vm.h"
#include "lug_opcodes.h"
#include "lug_debug.h"
#include "lug_memory.h"
#include "lug_stdlib.h"
#include "lug_filesys.h"

namespace fs = std::filesystem;

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

VMCore::VMCore(VM::Config config) 
    : strings(*this)
    , builtins(*this)
    , gc(*this)
    , compiler(*this)
    , modules(*this)
    , config(config)
{
    // We'll initialize the memory LugError at the start of the program
    // so that we don't have to allocate it later on.
    memoryError.type = InterpretResult::MEMORY_ERROR;
    memoryError.message = "unable to allocate further memory";

    stack.Reset();
}


InterpretResult VMCore::Interpret(std::string_view source, fs::path sourcePath) {
    try {
        ObjModule* module = compiler.Compile(source.data(), sourcePath);
        stack.Push(module);

        ObjClosure* closure = ObjClosure::New(*this, module->function, module);
        stack.Pop(); // module
        stack.Push(closure);
        Call(closure, 0);

        // Pop the return value. During the REPL, the last OP_RETURN
        // started to push the 'nil' value onto the very bottom of the
        // stack, causing a bunch of 'nil' values to accumulate at the bottom. 
        stack.Pop();

    } catch (LugError& error) {
        PushError(error);
        
        //! Not the best way right now to handle errors. Ideally the user'd like the stack to
        // rewind to how it was, but this is a good temporary fix.
        stack.Reset();
        return error.type;

    } catch (std::bad_alloc& error) {
        PushError(memoryError);
        stack.Reset();

        return lastError.type;
    } 

    return InterpretResult::OK;
}


/**
 * Gets a value's proto, if there is any.
 */
ObjThing* VMCore::GetProtoOf(Value value) const {
    Value* proto = builtins.Get(value.StringType());
    if (proto == nullptr || !proto->IsObj(ObjType::THING)) {
        return nullptr;
    }

    return proto->AsObj<ObjThing>();
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
 * Returns a string of the script's stack traceback.
 */
std::string VMCore::StackTraceback() {
    std::string traceback;
    traceback += "stack traceback:\n";
    for (int i = stack.frameCount - 1; i >= 0; i--) {
        CallFrame& frame = stack.frames[i];
        ObjModule* module = frame.closure->module;
        ObjFunction* function = frame.closure->function;
        std::size_t instruction = frame.ip - function->chunk.bytecode.Data() - 1;
        std::size_t line = function->chunk.GetLine(instruction);

        fs::path path{ module->path->characters };
        traceback += "    ";
        traceback += path.filename().string();
        traceback += ":";
        traceback += std::to_string(line);
        traceback += ": in ";

        if (function->name != nullptr) {
            traceback.append(function->name->characters);
            traceback += "()";
        } else {
            traceback += "script";
        }

        if (i != 0) traceback += '\n';
    }

    return traceback;
}


/**
 * 'Pushing' an error just means updating the lastError
 * and also calling the user-defined error callback.
 */
void VMCore::PushError(LugError error) {
    lastError = error;
    config.errorFn(error);
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

    builtins.Set(stack.Peek(1).AsObj<ObjString>(), stack.Peek(0));

    stack.Pop();
    stack.Pop();
}


ObjModule* VMCore::LoadModule(fs::path importerPath, fs::path modulePath) {
    auto name = config.resolveModuleFn(importerPath, modulePath);
    if (!name) {
        throw RuntimeError("could not find file '%s'\n", modulePath.string().c_str());
    }

    // If it's already cached in the vm...
    ObjString* moduleName = ObjString::FromLiteral(*this, modulePath.string().c_str());
    Value* cached = modules.Get(moduleName);
    if (cached && cached->IsObj(ObjType::MODULE)) {
        return cached->AsObj<ObjModule>();
    }

    auto source = config.loadModuleFn(*name);
    if (!source) {
        throw RuntimeError("could not load file '%s'\n", modulePath.string().c_str());
    }

    ObjModule* module = compiler.Compile((*source).data(), modulePath);
    stack.Push(module);
    modules.Set(moduleName, module);
    stack.Pop();

    return module;
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
        going through a reference. It is also able to access
        the cached modules. Let's mark those as well.
    */
    gc.MarkTable(builtins);
    gc.MarkTable(modules);

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


void VMCore::PushFrame(ObjClosure* closure, std::uint8_t argCount, std::uint8_t shared) {
    const ObjFunction::Arguments& parameters = closure->function->parameters;
    if (argCount != parameters.arity) {
        throw RuntimeError("expected %d arguments but got %d", parameters.arity, argCount);
    }

    // If the 'shared' parameters don't align...
    std::uint8_t masked = parameters.shared & ~shared;
    if ((masked) != 0) {
        int index = CountTrailingZeroes(masked);
        throw RuntimeError("expected a shared argument at argument %d", index + 1);
    }

    if (stack.frameCount == Stack::FRAMES_MAX) {
        throw RuntimeError("stack overflow");
    }

    stack.AddFrame(closure, stack.top - argCount - 1);
}


/**
 * This function will attempt to call any value of any type.
 * Returns whether a new stack frame was pushed.
 */
bool VMCore::CallValue(Value value, std::uint8_t argCount, std::uint8_t shared) {
    if (value.IsObj()) {
        switch (value.AsObj()->type) {
            case ObjType::CLOSURE: {
                PushFrame(value.AsObj<ObjClosure>(), argCount, shared);
                return true;
            }
            
            case ObjType::NATIVE: {               
                ObjNative* native = value.AsObj<ObjNative>();
                Value result = native->function(*this, stack.top - argCount, argCount);
                
                // Rewind the stack right before the ObjNative.
                stack.top -= argCount + 1;
                
                // Push the result of the function.
                stack.Push(result);
                return false;
            }
            
            case ObjType::THING: {
                // Check if the 'thing' has a '__call__' dunder 
                // attached to it.
                ObjThing* thing = value.AsObj<ObjThing>();
                const Value* callDunder = thing->Get("__call__");
                if (callDunder == nullptr) break;
                
                return CallValue(*callDunder, argCount, shared);;
            }

            default: {
                ObjThing* valueProto = GetProtoOf(value);
                if (valueProto != nullptr) return CallValue(valueProto, argCount, shared);
            }
        }
    }
    
    throw RuntimeError("attempted to call a '%s' value", value.StringType());
}


/**
 * This function will call a given callee and run the interpreter
 * until the function finishes.
 */
void VMCore::Call(ObjClosure* closure, std::uint8_t argCount, std::uint8_t shared) {
    PushFrame(closure, argCount, shared);
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
    std::size_t entryDepth = stack.frameCount - 1;
    CallFrame* frame = stack.GetFrame();
    ObjModule* module = frame->closure->module;

    bool isShortOp = false;

    #define READ_BYTE() (*frame->ip++)
    #define READ_STRING() (READ_CONSTANT().AsObj<ObjString>())
    #define READ_SHORT() ((READ_BYTE() << 8) | READ_BYTE())
    #define READ_CONSTANT() (frame->closure->function->chunk.constants[isShortOp ? READ_SHORT() : READ_BYTE()])

    #define JUMP_FORWARD(bytes) (frame->ip += bytes)
    #define JUMP_BACKWARD(bytes) (frame->ip -= bytes)

    #define BINARY_OP(op)                                   \
    do {                                                    \
        Value b = stack.Pop();                              \
        Value a = stack.Pop();                              \
        if (!a.IsNumber() || !b.IsNumber()) {               \
            throw RuntimeError("operands must be numbers"); \
        }                                                   \
        stack.Push(a.AsNumber() op b.AsNumber());           \
    } while (false)


    // Dispatch table shenanigans -------------------

    while (true) {
    #ifdef LUG_DEBUG_TRACE_EXECUTION
        std::printf("          ");
        stack.Print();

        Debug::DisassembleInstruction(
            frame->closure->function->chunk,
            static_cast<int>((frame->ip - frame->closure->function->chunk.bytecode.Data()))
        );
    #endif

        OpCode::Type instruction;
        switch (instruction = static_cast<OpCode::Type>(READ_BYTE())) {
            case (OpCode::CONSTANT): {
                Value constant = READ_CONSTANT();
                stack.Push(constant);
                break;
            }

            case (OpCode::SHORT): {
                isShortOp = true;
                continue;
            }

            case (OpCode::NEGATE): {
                Value value = stack.Pop();
                if (!value.IsNumber()) {
                    throw RuntimeError("operand must be a number");
                }

                stack.Push(-value.AsNumber());
                break;
            }

            case (OpCode::NOT): {
                Value value = stack.Pop();
                stack.Push(IsFalsy(value));
                break;
            }

            case (OpCode::TRUE): {
                stack.Push(true);
                break;
            }

            case (OpCode::FALSE): {
                stack.Push(false);
                break;
            }

            case (OpCode::NIL): {
                stack.Push(Value::Nil());
                break;
            }

            case (OpCode::ADD): {
                if (stack.Peek(0).IsObj(ObjType::STRING) && stack.Peek(1).IsObj(ObjType::STRING)) {
                    Concatenate();
                } else if (stack.Peek(0).IsNumber() && stack.Peek(1).IsNumber()) {
                    double b = stack.Pop().AsNumber();
                    double a = stack.Pop().AsNumber();

                    stack.Push(a + b);

                } else {
                    throw RuntimeError("can't add operands of type '%s' and '%s' together", 
                        stack.Peek(1).StringType(), 
                        stack.Peek(0).StringType()
                    );
                    return;
                }

                break;
            }

            case (OpCode::SUBTRACT): {
                BINARY_OP(-);
                break;
            }

            case (OpCode::MULTIPLY): {
                BINARY_OP(*);
                break;
            }

            case (OpCode::DIVIDE): {
                if (stack.Peek(0).IsNumber() && stack.Peek(0).AsNumber() == 0) {
                    throw RuntimeError("can't divide by zero");
                }

                BINARY_OP(/);
                break;
            }

            case (OpCode::MODULO): {
                Value bValue = stack.Pop();
                Value aValue = stack.Pop();
                if (!aValue.IsNumber() || !bValue.IsNumber()) {           
                    throw RuntimeError("operands must be numbers");
                }    

                double a = aValue.AsNumber();
                double b = bValue.AsNumber();
                stack.Push(a - std::floor(a / b) * b);
                
                break;
            }

            case (OpCode::EQUAL): {
                Value b = stack.Pop();
                Value a = stack.Pop();
                stack.Push(a == b);
                break;
            }

            case (OpCode::GREATER): {
                BINARY_OP(>);
                break;
            }
            
            case (OpCode::GREATER_EQUAL): {
                BINARY_OP(>=);
                break;
            }
            
            case (OpCode::LESS): {
                BINARY_OP(<);
                break;
            }
            
            case (OpCode::LESS_EQUAL): {
                BINARY_OP(<=);
                break;
            }

            case (OpCode::JUMP): {
                std::uint16_t numBytesToJump = READ_SHORT();
                JUMP_FORWARD(numBytesToJump);
                break;
            }

            case (OpCode::JUMP_IF_FALSE): {
                std::uint16_t numBytesToJump = READ_SHORT();
                if (IsFalsy(stack.Peek(0))) {
                    JUMP_FORWARD(numBytesToJump);
                }
                break;
            }

            case (OpCode::JUMP_IF_TRUE): {
                std::uint16_t numBytesToJump = READ_SHORT();
                if (IsTruthy(stack.Peek(0))) {
                    JUMP_FORWARD(numBytesToJump);
                }
                break;
            }

            case (OpCode::JUMP_IF_FALSE_OR_POP): {
                std::uint16_t numBytesToJump = READ_SHORT();
                if (IsFalsy(stack.Peek(0))) {
                    JUMP_FORWARD(numBytesToJump);
                } else {
                    stack.Pop();
                }
                break;
            }

            case (OpCode::JUMP_IF_TRUE_OR_POP): {
                std::uint16_t numBytesToJump = READ_SHORT();
                if (IsTruthy(stack.Peek(0))) {
                    JUMP_FORWARD(numBytesToJump);
                } else {
                    stack.Pop();
                }
                break;
            }

            case (OpCode::POP_JUMP_IF_FALSE): {
                std::uint16_t numBytesToJump = READ_SHORT();
                if (IsFalsy(stack.Pop())) {
                    JUMP_FORWARD(numBytesToJump);
                }

                break;
            }

            case (OpCode::POP_JUMP_IF_TRUE): {
                std::uint16_t numBytesToJump = READ_SHORT();
                if (IsTruthy(stack.Pop())) {
                    JUMP_FORWARD(numBytesToJump);
                }

                break;
            }

            case (OpCode::LOOP): {
                std::uint16_t numBytes = READ_SHORT();
                JUMP_BACKWARD(numBytes);
                break;
            }

            case (OpCode::COUNT_PREP): {
                std::uint16_t jumpToEnd = READ_SHORT();

                Value start = stack.Peek(2);
                Value stop = stack.Peek(1);
                Value step = stack.Peek(0);

                if (!start.IsNumber()) throw RuntimeError("'count' initial value must be a number");
                if (!stop.IsNumber()) throw RuntimeError("'count' limit value must be a number");
                if (!step.IsNumber()) throw RuntimeError("'count' step value must be a number");
                
                // Check if the loop would even run at all.
                if (step.AsNumber() == 0) {
                    throw RuntimeError("'count' step value cannot be zero");
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
                    JUMP_FORWARD(jumpToEnd);
                }

                break;
            }

            case (OpCode::COUNT_LOOP): {
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
                ASSERT(start.IsNumber(), "invalid start");
                ASSERT(limit.IsNumber(), "invalid limit");
                ASSERT(step.IsNumber(), "invalid step");   

                // Increment index.
                start = start.AsNumber() + step.AsNumber();
                if (step.AsNumber() > 0 ? 
                    start.AsNumber() <= limit.AsNumber() :
                    start.AsNumber() >= limit.AsNumber()
                ) {
                    // Repeat loop
                    JUMP_BACKWARD(jumpToStart);
                }

                break;
            }

            case (OpCode::FOREACH_PREP): {
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
                    ASSERT(listValue != nullptr, "the foreach index is out of range");
                    value = *listValue;
                    
                    if (static_cast<std::size_t>(key.AsInteger()) >= list->items.Count()) {
                        JUMP_FORWARD(jumpToEnd);
                    }
                } 
                
                else if (iterable.IsObj(ObjType::DICT)) {
                    ObjDict* dict = iterable.AsObj<ObjDict>();
                    Value& probingIndex = stack.Peek(3);
                    if (!probingIndex.IsNumber()) probingIndex = 0;

                    std::size_t index = static_cast<std::size_t>(probingIndex.AsInteger());
                    while (
                        index < dict->entries.capacity 
                        && dict->entries.entries[index].IsEmpty()
                    ) index++;

                    if (index == dict->entries.capacity) {
                        JUMP_FORWARD(jumpToEnd);
                    }

                    Entry& currentEntry = dict->entries.entries[index];
                    key = currentEntry.key;
                    value = currentEntry.value;
                    probingIndex = static_cast<int>(index);
                } 
                
                else {
                    throw RuntimeError("'foreach' must iterate over an iterable type, got '%s' instead", iterable.StringType());
                }

                break;
            }

            case (OpCode::FOREACH_LOOP): {
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
                        ASSERT(listValue != nullptr, "the foreach index is out of range");
                        value = *listValue;

                        JUMP_BACKWARD(jumpToStart);
                    }
                } 

                else if (iterable.IsObj(ObjType::DICT)) {
                    ObjDict* dict = iterable.AsObj<ObjDict>();
                    Value& probingIndex = stack.Peek(3);
                    ASSERT(probingIndex.IsInteger(), "invalid type for probing index");

                    std::size_t index = static_cast<std::size_t>(probingIndex.AsInteger());
                    do index++; while (
                        index < dict->entries.capacity 
                        && dict->entries.entries[index].IsEmpty()
                    );

                    if (index < dict->entries.capacity) {
                        Entry& currentEntry = dict->entries.entries[index];
                        key = currentEntry.key;
                        value = currentEntry.value;
                        probingIndex = static_cast<int>(index);
                        JUMP_BACKWARD(jumpToStart);
                    }
                }
                
                else {
                    ASSERT(false, "invalid iterable");
                }

                break;
            }

            case (OpCode::PLACEHOLDER): {
                ASSERT(false, "placeholder found during runtime");
            }
            
            case (OpCode::POP): {
                stack.Pop();
                break;
            }

            case (OpCode::DUPLICATE): {
                stack.Push(stack.Peek(0));
                break;
            }

            case (OpCode::COPY): {
                Value value = stack.Peek(0);
                Value copy = Deepcopy(value);
                stack.Pop();
                stack.Push(copy);

                break;
            }

            case (OpCode::DEFINE_GLOBAL): {
                /*
                    The operand of DEFINE_GLOBAL is a
                    string constant, or the name of the
                    global. The value of the global
                    is the value on top of the stack.
                */
                ObjString* string = READ_STRING();
                module->globals.Set(string, stack.Peek(0));
                stack.Pop();

                break;
            }

            case (OpCode::GET_GLOBAL): {
                ObjString* name = READ_STRING();
                Value* value = module->globals.Get(name);

                // If we can't find it in the module's globals, try getting it from
                // the builtins.
                if (value == nullptr) value = builtins.Get(name);
                if (value == nullptr) {
                    throw RuntimeError("undefined variable '%s'", name->characters);
                }

                stack.Push(*value);
                break;
            }
            
            case (OpCode::SET_GLOBAL): {
                ObjString* name = READ_STRING();

                // We assign the name to the value at the top of the stack.
                Value& value = stack.Peek(0);
                bool isNewKey = module->globals.Set(name, value);
                
                if (isNewKey) {
                    module->globals.Delete(name);
                    throw RuntimeError("undefined variable '%s'", name->characters);
                }

                break;
            }

            case (OpCode::GET_LOCAL): {
                std::uint8_t slot = READ_BYTE();
                stack.Push(frame->slots[slot]);

                break;
            }

            case (OpCode::SET_LOCAL): {
                std::uint8_t slot = READ_BYTE();
                frame->slots[slot] = stack.Peek(0);
                
                break;
            }

            case (OpCode::GET_UPVALUE): {
                std::uint8_t slot = READ_BYTE();
                stack.Push(*frame->closure->upvalues[slot]->location);

                break;
            }

            case (OpCode::SET_UPVALUE): {
                std::uint8_t slot = READ_BYTE();
                *frame->closure->upvalues[slot]->location = stack.Peek(0);

                break;
            }

            case (OpCode::CLOSE_UPVALUE): {
                CloseUpvalues(stack.top - 1);
                stack.Pop();

                break;
            }

            case (OpCode::CALL): {
                std::uint8_t argCount = READ_BYTE();
                
                // Attempt to call the callee, whether it is of any value type.
                if (CallValue(stack.Peek(argCount), argCount, 0b0)) {
                    frame = stack.GetFrame();
                    module = frame->closure->module;
                }
                
                break;
            }
            
            case (OpCode::CALL_SHARED): {
                std::uint8_t argCount = READ_BYTE();
                std::uint8_t shared = READ_BYTE();

                if (CallValue(stack.Peek(argCount), argCount, shared)) {
                    frame = stack.GetFrame();
                    module = frame->closure->module;
                };
                
                break;
            }

            case (OpCode::SELF): {
                ObjString* methodName = READ_STRING();
                Value object = stack.Pop();
                ObjThing* thing = nullptr;

                // If the object is of type 'thing', then 'thing' is
                // the object.
                if (object.IsObj(ObjType::THING)) {
                    thing = object.AsObj<ObjThing>();

                // If it's not a 'thing', we'll see if the object has a
                // proto.
                } else if (GetProtoOf(object) != nullptr) {
                    thing = GetProtoOf(object);
                }

                // If we still haven't gotten the 'thing' yet, we throw an error.
                if (thing == nullptr) {
                    throw RuntimeError(
                        "method '%s' does not exist in value of type '%s'", 
                        methodName->characters, 
                        object.StringType()
                    );
                }
                
                const Value* value = thing->Get(methodName);
                stack.Push(*value);
                stack.Push(object);

                break;
            }

            case (OpCode::CLOSURE): {
                ObjFunction* function = READ_CONSTANT().AsObj<ObjFunction>();
                ObjClosure* closure = ObjClosure::New(*this, function, module);
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

                break;
            }
            
            case (OpCode::LIST): {
                ObjList* newList = ObjList::New(*this);
                stack.Push(newList);

                break;
            }

            case (OpCode::DICT): {
                ObjDict* newDict = ObjDict::New(*this);
                stack.Push(newDict);

                break;
            }

            case (OpCode::THING): {
                ObjThing* newThing = ObjThing::New(*this);
                stack.Push(newThing);

                break;
            }

            case (OpCode::CATEGORY): {
                ObjCategory* newCategory = ObjCategory::New(*this);
                stack.Push(newCategory);

                break;
            }

            case (OpCode::APPEND): {
                ObjList* list = stack.Peek(1).AsObj<ObjList>();
                list->Append(stack.Pop());
                
                break;
            }

            case (OpCode::DEFINE_ENTRY): {
                ObjDict* dictionary = stack.Peek(2).AsObj<ObjDict>();
                const Value& key = stack.Peek(1);
                const Value& value = stack.Peek(0);

                dictionary->Set(key, value);

                stack.Pop();
                stack.Pop();

                break;
            }

            case (OpCode::GET_INDEX): {
                //    stack.Peek(1)     stack.Peek(0)
                // [ collection ][ index ]
                //                 ^^^^^ stackTop. You are here.
                Value& collection = stack.Peek(1);
                if (!collection.IsObj(ObjType::LIST) && !collection.IsObj(ObjType::DICT)) {
                    throw RuntimeError("value of type '%s' is not indexable", collection.StringType());
                }
                
                switch (collection.AsObj()->type) {
                    case ObjType::LIST: {
                        if (!stack.Peek(0).IsInteger()) throw RuntimeError("list index must be an integer");
                        
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
                            
                            throw RuntimeError("could not find key in dictionary");
                        }

                        stack.Pop(); // Key
                        stack.Pop(); // Collection
                        
                        stack.Push(*value);

                        break;
                    }
                }

                break;
            }
            
            case (OpCode::SET_INDEX): {
                //     stack.Peek(2)    stack.Peek(1)    stack.Peek(0)
                // [ collection ][ index ][ newValue ]
                //                                    ^^^^^^ stackTop. You are here.
                Value& collection = stack.Peek(2);
                if (!collection.IsObj(ObjType::LIST) && !collection.IsObj(ObjType::DICT)) {
                    throw RuntimeError("value of type '%s' is not indexable", stack.Peek(2).StringType());
                }

                Value newValue = stack.Peek(0);

                switch (collection.AsObj()->type) {
                    case ObjType::LIST: {
                        // Lists must always have integers as indices.
                        if (!stack.Peek(1).IsInteger()) throw RuntimeError("list index must be an integer");
                        
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

                break;
            }

            case (OpCode::GET_ATTRIBUTE): {
                ObjString* attributeName = READ_STRING();

                if (stack.Peek(0).IsObj(ObjType::CATEGORY)) {
                    ObjCategory* category = stack.Peek(0).AsObj<ObjCategory>();
                    const Value* type = category->GetType(attributeName);
                    if (type == nullptr) {
                        throw RuntimeError("type '%s' does not exist in category", attributeName->characters);
                    }

                    stack.Pop(); // Category
                    stack.Push(*type);

                    break;
                }

                if (stack.Peek(0).IsObj(ObjType::THING)) {
                    ObjThing* thing = stack.Peek(0).AsObj<ObjThing>();
                    const Value* value = thing->Get(attributeName);
                    if (value == nullptr) {
                        throw RuntimeError("thing has no attribute named '%s'", attributeName->characters);
                    }
                    
                    stack.Pop(); // Thing
                    stack.Push(*value);

                    break;
                }

                ObjThing* proto = GetProtoOf(stack.Peek(0));
                if (proto && proto->Get(attributeName) != nullptr) {
                    const Value* value = proto->Get(attributeName);
                    if (value == nullptr) {
                        throw RuntimeError("could not find '%s' in value", attributeName->characters);
                    }

                    stack.Pop();
                    stack.Push(*value);

                    break;
                }
                
                throw RuntimeError(
                    "type '%s' does not support attributes", 
                    stack.Peek(0).StringType()
                );

                break;
            }

            case (OpCode::SET_ATTRIBUTE): {
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
                    
                    break;
                }

                if (object.IsObj(ObjType::CATEGORY)) {
                    ObjCategory* category = object.AsObj<ObjCategory>();

                    // Note that we cannot add new kinds after the category literal.
                    // category->AddType will error for us when we try and re-define
                    // an attribute that has already been defined.
                    category->AddType(attributeName, value);
                    stack.Pop();

                    break;
                }

                throw RuntimeError(
                    "attribute '%s' does not exist in value of type '%s'", 
                    attributeName->characters, 
                    stack.Peek(0).StringType()
                );
            }

            case (OpCode::RETURN): {
                Value result = stack.Pop();
                CloseUpvalues(frame->slots);

                stack.RemoveFrame();
                stack.Push(result);

                if (stack.frameCount == entryDepth) return;

                frame = stack.GetFrame();
                module = frame->closure->module;

                break;
            }
        }

        isShortOp = false;
    }

    #undef READ_BYTE
    #undef READ_STRING
    #undef READ_SHORT
    #undef READ_CONSTANT

    #undef JUMP_FORWARD
    #undef JUMP_BACKWARD

    #undef BINARY_OP
}


// Luggage public header methods -----------------------------------

VM::VM(const VM::Config& config) 
: core(std::make_unique<VMCore>(config)) 
, config(config)
{
    
}


VM::~VM() = default;


InterpretResult VM::InterpretFile(fs::path filePath) {
    auto resolved = Filesystem::ResolveModulePath("", filePath);
    if (!resolved) {
        LugError fileError = core->RuntimeError("could not find script '%s'", filePath.string().c_str());
        fileError.type = InterpretResult::FILE_ERROR;
        core->PushError(fileError);

        return InterpretResult::FILE_ERROR;
    }

    auto source = Filesystem::ReadModule(filePath);
    if (!source) {
        LugError fileError = core->RuntimeError("could not load file '%s'", filePath.string().c_str());
        fileError.type = InterpretResult::FILE_ERROR;
        core->PushError(fileError);

        return InterpretResult::FILE_ERROR;
    }

    return core->Interpret(*source, filePath);
}


InterpretResult VM::Interpret(std::string_view source, std::string_view scriptName) {
    return core->Interpret(source, scriptName);
}


void VM::CollectGarbage() { 
    core->CollectGarbage(); 
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

// Config --------------------------

std::optional<fs::path> DefaultResolveModule(
    const fs::path& importerPath, 
    const fs::path& requestedPath
) {
    return Filesystem::ResolveModulePath(importerPath, requestedPath);
}


std::optional<std::string> DefaultFileLoader(const fs::path& filePath) {
    return Filesystem::ReadModule(filePath);
}


void DefaultWriter(std::string_view string) {
    std::fwrite(string.data(), sizeof(char), string.size(), stdout);
}

}