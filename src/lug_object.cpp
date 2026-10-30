#include <cstdio>
#include <cstring>
#include <cstdarg>

#include "lug_object.h"
#include "lug_vm.h"
#include "lug_gc.h"

namespace Luggage {

// Helper functions ------------------------------------

template<typename T>
static T* AllocateObject(VMCore& vm) {
    /*
        Every heap allocation needs to go through Reallocate for the 
        garbage collector. We cannot do 'new <Object>' to allocate
        to the heap, since it won't count the number of bytes we 
        allocated and add it to the vm's bytesAllocated stat, 
        so we route it to this function.
    */
    void* allocatedMemory = Memory::Reallocate(vm, nullptr, 0, sizeof(T));
    
    // This syntax allows us to create the Obj at the given
    // memory block. This also calls the constructor of
    // the Obj, which standard malloc() / Reallocate() does 
    // not do.
    T* object = new (allocatedMemory) T(vm);

    object->size = sizeof(T);
    vm.AddObject(object);

#ifdef LUG_DEBUG_LOG_GC
    std::printf(
        "%p allocate %zu for %s\n", 
        static_cast<void*>(object), 
        object->size, 
        Obj::StringType(object->type)
    );
#endif

    return object;
}


/**
 * Hashes a string with the FNV-1a algorithm.
 */
static std::uint32_t HashString(const char* string, std::size_t length) {
    std::uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= static_cast<std::uint8_t>(string[i]);
        hash *= 16777619;
    }

    return hash;
}


/**
 * Hashes a pointer. This simply converts the memory address into
 * a std::uint32_t.
 */
static std::uint32_t HashPointer(void* pointer) {
    return reinterpret_cast<std::uintptr_t>(pointer) & UINT32_MAX;
}


// Obj methods ------------------------------------

void Obj::Print() {
    std::printf("unknown object type '%d'\n", static_cast<int>(type));
    UNREACHABLE();
}


ObjString* Obj::ToString() {
    std::printf("object of type '%d' has no tostring function", static_cast<int>(type));
    UNREACHABLE();
}


const char* Obj::StringType(ObjType type) {
    switch (type) {
        case ObjType::STRING:   return "string";

        case ObjType::NATIVE:   
        case ObjType::CLOSURE:  
            return "function";

        case ObjType::MODULE:
            return "module";

        case ObjType::LIST:     return "list";
        case ObjType::DICT:     return "dict";
        case ObjType::THING:    return "thing";
        case ObjType::CATEGORY: return "category";
        case ObjType::UPVALUE:  return "upvalue";

        // These are behind-the-scenes objects that
        // the user probably can't access.
        case ObjType::FUNCTION: return "FUNCTION_IMPL";

        default: 
            std::printf("unknown object type '%d'\n", static_cast<int>(type));
            UNREACHABLE();
    }
}


// ObjString methods ------------------------------------

/**
 * Allocates an ObjString* onto the heap and returns its memory
 * address.
 */
static ObjString* AllocateString(VMCore& vm, char* characters, std::size_t length, std::uint32_t hash) {
    ObjString* string = AllocateObject<ObjString>(vm);
    string->length = length;
    string->characters = characters;
    string->hash = hash;

    // When we set the string with vm.strings.Set, there's
    // a possibility that it might trigger a GC. We should push
    // the string on the stack so that in the case that it does,
    // the string isn't collected.
    vm.stack.Push(string);

    // Add the string to the vm's interned string table
    vm.strings.Set(string, Value::Nil());

    // Then pop it off once we're done.
    vm.stack.Pop();

    return string;
}

/**
 * Creates an ObjString from a const-string, usually part of
 * the source code itself. This method will create a new heap
 * allocation for the passed-in string, since it cannot take
 * ownership of the original one.
 */
ObjString* ObjString::FromSource(VMCore& vm, const char* original, std::size_t length) {
    std::uint32_t hash = HashString(original, length);

    // Check if the string is already in the vm's string
    // interned hashset. If so, we don't need to allocate
    // anything at all, we can just return it!
    ObjString* interned = vm.strings.FindString(original, length, hash);
    if (interned != nullptr) return interned;
    
    /*
        Note that we allocate one extra byte of memory to
        store the null terminator at the end of the string.
        When we pass in the lexeme of a token in copyString(),
        the lexeme is part of the greater source string itself,
        which means it's not terminated.

        box variable <- 5;
            ^^^^^^^^
            const char* chars = ['v', 'a', 'r', 'i', ...] (no \0)
    */
    char* heapChars = Memory::Allocate<char>(vm, length + 1);
    
    // We'll then copy the original characters onto our
    // newly allocated space.
    std::memcpy(heapChars, original, length);

    // Don't forget the null terminator.
    heapChars[length] = '\0';

    return AllocateString(vm, heapChars, length, hash);
}


/**
 * You can only use this kind of constructor if you are sure
 * that the passed-in string ends with a null terminator, for instance,
 * string literals are safe. However, strings that are part of a bigger
 * string (i.e. the source code) are not safe to copy from because that 
 * smaller part doesn't have a null terminator, since it
 * will start to overflow to the other words after it.
 * 
 * ['h', 'e', 'l', 'l', 'o', '\0'] <- yes
 * [... 's', 'u', 'b', ...] <- no guarantee of null terminator at the end of this
 *      ^^^^^^^^^^^^^
 * part of a larger string!
 */
ObjString* ObjString::FromLiteral(VMCore& vm, const char* literal) {
    return FromSource(vm, literal, std::strlen(literal));
}

/**
 * Instead of copying the characters/allocating them to a new
 * block of memory on the heap like in ObjString::FromSource, this function
 * claims ownership of the string you give it.
 */
ObjString* ObjString::FromAllocated(VMCore& vm, char* chars, std::size_t length) {
    std::uint32_t hash = HashString(chars, length);
    
    // Check if the string already exists in the 
    // vm's interned-string hashset
    ObjString* interned = vm.strings.FindString(chars, length, hash);
    if (interned != nullptr) {
        Memory::FreeArray<char>(vm, chars, length + 1);
        return interned;
    }

    return AllocateString(vm, chars, length, hash);
}


/**
 * Creates an ObjString with a format string.
 */
ObjString* ObjString::FromFormat(VMCore& vm, const char* format, ...) {
    std::va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);

    int sizeNeeded = std::vsnprintf(nullptr, 0, format, args);
    va_end(args);
    ASSERT(sizeNeeded >= 0, "vsnprintf formatting error");

    std::size_t size = static_cast<std::size_t>(sizeNeeded) + 1;
    char* formatted = Memory::Allocate<char>(vm, size);
    std::vsnprintf(formatted, size, format, copy);
    va_end(copy);

    return FromAllocated(vm, formatted, sizeNeeded);
}


ObjString::~ObjString() {
    // +1 for the null terminator at the end
    Memory::FreeArray<char>(vm, characters, length + 1);
}


void ObjString::Print() {
    std::printf("%s", characters);
}


ObjString* ObjString::ToString() {
    return this; 
}


// ObjFunction methods ------------------------------------

ObjFunction* ObjFunction::New(VMCore& vm) {
    ObjFunction* function = AllocateObject<ObjFunction>(vm);
    // No need to compute hash for ObjFunction. ObjClosure is what will
    // actually be used during runtime.

    return function;
}


ObjFunction::~ObjFunction() {

}


void ObjFunction::Print() {
    if (name != nullptr) {
        std::printf("<fn %s>", name->characters);
        return;
    }

    std::printf("<script>");
}


void ObjFunction::Blacken(GC& gc) {
    gc.MarkObject(name);                            
    gc.MarkVector(chunk.constants);
}


// ObjModule methods ------------------------------------

ObjModule* ObjModule::New(VMCore& vm, ObjFunction* function, std::filesystem::path path) {
    ObjModule* module = AllocateObject<ObjModule>(vm);
    module->function = function;

    // Since FromLiteral may trigger an allocation and module isn't rooted yet, we 
    // temporarily root it by pushing it to the stack.
    vm.stack.Push(module);
    module->path = ObjString::FromLiteral(vm, path.string().c_str());
    vm.stack.Pop();

    return module;
}


void ObjModule::Print() {
    std::printf("<module %s>", function->name ? function->name->characters : "script");
}


void ObjModule::Blacken(GC& gc) {
    gc.MarkTable(globals);
    gc.MarkObject(function);
    gc.MarkObject(path);
}


// ObjUpvalue methods ------------------------------------

ObjUpvalue* ObjUpvalue::New(VMCore& vm, Value* slot) {
    ObjUpvalue* upvalue = AllocateObject<ObjUpvalue>(vm);
    upvalue->location = slot;
    // No need to calculate hash for upvalues.

    return upvalue;
}


void ObjUpvalue::Print() {
    std::printf("upvalue");
}


void ObjUpvalue::Blacken(GC& gc) {
    gc.MarkValue(closed);
}


// ObjClosure methods ------------------------------------

ObjClosure::~ObjClosure() {
    /*
        We mustn't free the ObjFunction because the
        ObjClosure doesn't actually *own* the function.
        Many ObjClosures can be pointing to the same 
        function, and we want to make sure that the
        ObjFunction stays until all objects referencing it
        are gone.
    */

    // We should free the upvalue array, however...
    Memory::FreeArray<ObjUpvalue*>(vm, upvalues, upvalueCount);
}


ObjClosure* ObjClosure::New(VMCore& vm, ObjFunction* function, ObjModule* module) {
    ObjUpvalue** upvalues = Memory::Allocate<ObjUpvalue*>(vm, function->upvalueCount);
    for (std::size_t i = 0; i < function->upvalueCount; i++) {
        upvalues[i] = nullptr;
    }
    
    ObjClosure* newClosure = AllocateObject<ObjClosure>(vm);
    newClosure->function = function;
    newClosure->module = module;
    newClosure->upvalues = upvalues;
    newClosure->upvalueCount = function->upvalueCount;
    newClosure->hash = HashPointer(newClosure);

    return newClosure;
}


void ObjClosure::Print() {
    function->Print();
}


ObjString* ObjClosure::ToString() {
    if (function->name == nullptr) {
        return ObjString::FromLiteral(vm, "<script>");
    }
            
    return ObjString::FromFormat(vm, "<fn %s>", function->name->characters);
}


void ObjClosure::Blacken(GC& gc) {
    gc.MarkObject(function);
    gc.MarkObject(module);

    for (std::size_t i = 0; i < upvalueCount; i++) {
        gc.MarkObject(upvalues[i]);
    }
}


// ObjNative methods ------------------------------------

ObjNative* ObjNative::New(VMCore& vm, const char* name, NativeFn function) {
    ObjNative* native = AllocateObject<ObjNative>(vm);
    native->function = function;
    native->name = name;

    return native;
}


void ObjNative::Print() {
    std::printf("<native %s>", name);
}


ObjString* ObjNative::ToString() {
    return ObjString::FromFormat(vm, "<native %s>", name);
}


// ObjList methods ------------------------------------

ObjList* ObjList::New(VMCore& vm) {
    ObjList* list = AllocateObject<ObjList>(vm);
    list->hash = HashPointer(list);
    return list;
}


void ObjList::Print() {
    std::printf("[ ");
    for (std::size_t i = 0; i < items.Count(); i++) {
        Value::Print(items[i]);
        if (i != items.Count() - 1) std::printf(", ");
    }

    std::printf(" ]");
}


ObjString* ObjList::ToString() {
    return ObjString::FromFormat(vm, "<list %p>", static_cast<void*>(this));
}


void ObjList::Blacken(GC& gc) {
    gc.MarkVector(items);
}


void ObjList::Append(Value value) {
    items.PushBack(value);
}


const Value* ObjList::Get(int index) const {
    index = ClampIndex(index);
    if (index == -1) return nullptr;

    return &items[index];
}


int ObjList::ClampIndex(int index) const {
    int numItems = static_cast<int>(items.Count());

    // This handles the wrap-around case for negative numbers.
    if (index < 0) {
        return ((index % numItems) + numItems) % numItems; 
    }

    // -1 because itemCount starts at 1, while indices start at 0
    if (index > numItems - 1) {
        return -1;
    }

    return index;
}


void ObjList::Set(int index, Value value) {
    index = ClampIndex(index);
    if (index == -1) throw vm.RuntimeError("index '%d' out of range", index);
    items[index] = value;
}

// ObjDict methods ------------------------------------

ObjDict* ObjDict::New(VMCore& vm) {
    ObjDict* newDict = AllocateObject<ObjDict>(vm);
    newDict->hash = HashPointer(newDict);
    return newDict;
}


void ObjDict::Print() {
    std::printf("{ ");

    std::size_t numPrinted = 0;

    for (std::size_t i = 0; i < entries.capacity; i++) {
        const Entry& entry = entries[i];

        if (entry.key.type == ValueType::TOMBSTONE) continue;
        if (entry.key.IsNil()) continue;
        
        Value::Print(entry.key);
        std::printf(" : ");
        Value::Print(entry.value);
        
        if (numPrinted != entries.count - 1) std::printf(", ");
        numPrinted++;
    }

    std::printf(" }");
}


ObjString* ObjDict::ToString() {
    return ObjString::FromFormat(vm, "<dict %p>", static_cast<void*>(this));
}


void ObjDict::Blacken(GC& gc) {
    gc.MarkTable(entries);
}


const Value* ObjDict::Get(const Value& key) const {
    return entries.Get(key);
}


void ObjDict::Set(Value key, Value value) {
    entries.Set(key, value);
}


void ObjDict::Delete(Value key) {
    entries.Delete(key);
}


// ObjThing methods ------------------------------------

ObjThing* ObjThing::New(VMCore& vm) {
    ObjThing* newThing = AllocateObject<ObjThing>(vm);
    newThing->hash = HashPointer(newThing);
    return newThing;
}


void ObjThing::Print() {
    std::printf("<thing>");
}


ObjString* ObjThing::ToString() {
    const Value* stringDunder = Get("__tostring__");

    // If we find a __tostring__ dunder in our attributes,
    // we should call it.
    if (stringDunder != nullptr) {
        vm.stack.Push(*stringDunder);
        vm.stack.Push(this);

        // We will 'share' the thing.
        vm.CallValue(*stringDunder, 1, 0b1);
        Value result = vm.stack.Pop();
        if (!result.IsObj(ObjType::STRING)) {
            throw vm.RuntimeError("'__tostring__' must return a string, returned '%s' instead", result.StringType());
        }

        return result.AsObj<ObjString>();
    }

    return ObjString::FromLiteral(vm, "<thing>");
}


void ObjThing::Blacken(GC& gc) {
    gc.MarkTable(attributes);
} 


const Value* ObjThing::Get(ObjString* attribute) const {
    Value* value = attributes.Get(attribute);
    if (value != nullptr) {
        return value;
    }

    Value* protoValue = attributes.Get("__proto__");
    if (protoValue == nullptr || !protoValue->IsObj(ObjType::THING)) {
        return nullptr;
    }

    ObjThing* proto = protoValue->AsObj<ObjThing>();
    return proto->Get(attribute);
}


const Value* ObjThing::Get(const char* attribute) const {
    return Get(ObjString::FromLiteral(vm, attribute));
}


void ObjThing::Set(ObjString* attribute, Value value) {
    attributes.Set(attribute, value);
}


void ObjThing::Set(const char* attribute, Value value) {
    ObjString* attributeName = ObjString::FromLiteral(vm, attribute);
    vm.stack.Push(attributeName);
    Set(attributeName, value);
    vm.stack.Pop();
}


void ObjThing::Delete(Value attribute) {
    attributes.Delete(attribute);
}

// ObjCategory methods ------------------------------------

ObjCategory* ObjCategory::New(VMCore& vm) {
    ObjCategory* newCategory = AllocateObject<ObjCategory>(vm);
    return newCategory;
}


void ObjCategory::Print() {
    std::printf("<category>");
}


ObjString* ObjCategory::ToString() {
    return ObjString::FromFormat(vm, "<category %p>", static_cast<void*>(this));
}


void ObjCategory::Blacken(GC& gc) {
    gc.MarkTable(types);
}


void ObjCategory::AddType(ObjString* typeName, Value& index) {
    bool isNewType = true;
    if (index.IsNil()) {
        isNewType = types.Set(typeName, currentIndex);
    } else {
        if (!index.IsInteger()) throw vm.RuntimeError("cannot assign a category type as '%s'", index.StringType());
        
        isNewType = types.Set(typeName, index);
        currentIndex = index.AsInteger();
    }

    if (!isNewType) {
        throw vm.RuntimeError("cannot re-define category type '%s'", typeName->characters);
    }
    
    currentIndex++;
}


const Value* ObjCategory::GetType(ObjString* typeName) {
    return types.Get(typeName);
}


}