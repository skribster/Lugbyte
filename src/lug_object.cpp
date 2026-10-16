#include <cstdio>
#include <cstring>

#include "lug_object.h"
#include "lug_memory.h"
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

        case ObjType::DICTIONARY:   
            return "dictionary";

        case ObjType::UPVALUE:      
            return "upvalue";

        // These are behind-the-scenes objects that
        // the user probably can't access.
        case ObjType::FUNCTION: 
        default:
            std::printf("objtype: %d\n", (int)type);
            UNREACHABLE();
    }
}


// ObjString methods ------------------------------------

static std::uint32_t HashString(const char* string, std::size_t length) {
    std::uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= static_cast<std::uint8_t>(string[i]);
        hash *= 16777619;
    }

    return hash;
}


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
ObjString* ObjString::CopyFrom(VMCore& vm, const char* original, std::size_t length) {
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
 * Instead of copying the characters/allocating them to a new
 * block of memory on the heap like in ObjString::CopyFrom, this function
 * claims ownership of the string you give it.
 */
ObjString* ObjString::TakeFrom(VMCore& vm, char* chars, std::size_t length) {
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
    function->arity = 0;
    function->name = nullptr;

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
    Memory::FreeArray<Value>(vm, items, itemCapacity);
}


ObjList* ObjList::New(VMCore& vm) {
    ObjList* list = AllocateObject<ObjList>(vm);
    return list;
}


void ObjList::Print() {
    std::printf("[ ");
    for (std::size_t i = 0; i < itemCount; i++) {
        Value::Print(items[i]);
        if (i != itemCount - 1) std::printf(", ");
    }

    std::printf(" ]");
}


void ObjList::Append(Value value) {
    if (itemCount >= itemCapacity) {
        std::size_t oldCapacity = itemCapacity;
        itemCapacity = Memory::GrowCapacity(oldCapacity);
        items = Memory::GrowArray<Value>(vm, items, oldCapacity, itemCapacity);
    }

    items[itemCount] = value;
    itemCount++;
}


Value ObjList::Get(int index) const {
    return items[index];
}


void ObjList::Set(int index, Value value) {
    items[index] = value;
}

// ObjDictionary methods ------------------------------------

ObjDictionary* ObjDictionary::New(VMCore& vm) {
    ObjDictionary* newDictionary = AllocateObject<ObjDictionary>(vm);
    return newDictionary;
}


ObjDictionary::~ObjDictionary() {

}


void ObjDictionary::Print() {
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


Value ObjDictionary::Get(Value key) const {
    Value* value = dictionary.Get(key);

    if (value == nullptr) return Value::Nil();
    return *value;
}


void ObjDictionary::Set(Value key, Value value) {
    dictionary.Set(key, value);
}


// ObjThing methods ------------------------------------

ObjThing::~ObjThing() {

}


ObjThing* ObjThing::New(VMCore& vm) {
    ObjThing* newThing = AllocateObject<ObjThing>(vm);
    return newThing;
}


void ObjThing::Print() {
    std::printf("<thing>");
}


Value ObjThing::Get(Value key) const {
    Value* value = attributes.Get(key);

    if (value == nullptr) return Value::Nil();
    return *value;
}   


void ObjThing::Set(Value attribute, Value value) {
    attributes.Set(attribute, value);
}


void ObjThing::Set(const char* attribute, Value value) {
    ObjString* attributeName = ObjString::CopyFrom(vm, attribute, std::strlen(attribute));
    Set(attributeName, value);
}


}