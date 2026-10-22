#include <cstdio>
#include <cstring>
#include <cstdarg>

#include "lug_object.h"
#include "lug_vm.h"

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
    std::printf("Unknown object");
    UNREACHABLE();
}


const char* Obj::StringType(ObjType type) {
    switch (type) {
        case ObjType::STRING:       
            return "string";

        case ObjType::NATIVE:
        case ObjType::CLOSURE:     
            return "function";

        case ObjType::LIST:         
            return "list";

        case ObjType::DICT:   
            return "dict";

        case ObjType::UPVALUE:      
            return "upvalue";

        case ObjType::THING: {
            return "thing";
        }

        // These are behind-the-scenes objects that
        // the user probably can't access.
        case ObjType::FUNCTION: 
        default:
            std::printf("objtype: %d\n", (int)type);
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

    // Add the string to the vm's interned string table
    vm.strings.Set(string, Value::Nil());

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
    if (name == nullptr) {
        std::printf("<script>");
        return;
    }
            
    std::printf("<fn %s>", name->characters);
}

// ObjUpvalue methods ------------------------------------

ObjUpvalue::~ObjUpvalue() {

}


ObjUpvalue* ObjUpvalue::New(VMCore& vm, Value* slot) {
    ObjUpvalue* upvalue = AllocateObject<ObjUpvalue>(vm);
    upvalue->location = slot;
    // No need to calculate hash for upvalues.

    return upvalue;
}


void ObjUpvalue::Print() {
    std::printf("upvalue");
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


ObjClosure* ObjClosure::New(VMCore& vm, ObjFunction* function) {
    ObjUpvalue** upvalues = Memory::Allocate<ObjUpvalue*>(vm, function->upvalueCount);
    for (std::size_t i = 0; i < function->upvalueCount; i++) {
        upvalues[i] = nullptr;
    }
    
    ObjClosure* newClosure = AllocateObject<ObjClosure>(vm);
    newClosure->function = function;
    newClosure->upvalues = upvalues;
    newClosure->upvalueCount = function->upvalueCount;
    newClosure->hash = HashPointer(newClosure);

    return newClosure;
}


void ObjClosure::Print() {
    function->Print();
}


// ObjNative methods ------------------------------------

ObjNative* ObjNative::New(VMCore& vm, const char* name, NativeFn function) {
    ObjNative* native = AllocateObject<ObjNative>(vm);
    native->function = function;
    native->name = name;

    return native;
}


ObjNative::~ObjNative() {
    
}


void ObjNative::Print() {
    std::printf("<native %s>", name);
}

// ObjList methods ------------------------------------

ObjList::~ObjList() {
    
}


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


void ObjList::Append(Value value) {
    items.PushBack(value);
}


const Value& ObjList::Get(int index) const {
    return items[index];
}


void ObjList::Set(int index, Value value) {
    items[index] = value;
}

// ObjDict methods ------------------------------------

ObjDict* ObjDict::New(VMCore& vm) {
    ObjDict* newDict = AllocateObject<ObjDict>(vm);
    newDict->hash = HashPointer(newDict);
    return newDict;
}


ObjDict::~ObjDict() {

}


void ObjDict::Print() {
    std::printf("{ ");

    std::size_t numPrinted = 0;

    for (std::size_t i = 0; i < dictionary.capacity; i++) {
        Entry& entry = dictionary.entries[i];

        if (entry.key.type == ValueType::TOMBSTONE) continue;
        if (entry.key.IsNil()) continue;
        
        Value::Print(entry.key);
        std::printf(" : ");
        Value::Print(entry.value);
        
        if (numPrinted != dictionary.count - 1) std::printf(", ");
        numPrinted++;
    }

    std::printf(" }");
}


Value ObjDict::Get(const Value& key) const {
    Value* value = dictionary.Get(key);

    if (value == nullptr) return Value::Nil();
    return *value;
}


void ObjDict::Set(Value key, Value value) {
    dictionary.Set(key, value);
}


void ObjDict::Delete(Value key) {
    dictionary.Delete(key);
}


// ObjThing methods ------------------------------------

ObjThing::~ObjThing() {

}


ObjThing* ObjThing::New(VMCore& vm) {
    ObjThing* newThing = AllocateObject<ObjThing>(vm);
    newThing->hash = HashPointer(newThing);
    return newThing;
}


void ObjThing::Print() {
    std::printf("<thing>");
}


Value ObjThing::Get(Value key) const {
    Value* value = attributes.Get(key);
    if (value != nullptr) return *value;

    Value* protoValue = attributes.Get("__proto__");
    if (protoValue == nullptr || !protoValue->IsObj(ObjType::THING)) return Value::Nil();

    ObjThing* proto = protoValue->AsObj<ObjThing>();
    return proto->Get(key);
}   


Value ObjThing::Get(const char* key) const {
    return Get(ObjString::FromLiteral(vm, key));
}   


void ObjThing::Set(Value attribute, Value value) {
    attributes.Set(attribute, value);
}


void ObjThing::Set(const char* attribute, Value value) {
    ObjString* attributeName = ObjString::FromLiteral(vm, attribute);
    Set(attributeName, value);
}


}