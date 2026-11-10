#include <cstdio>
#include <cstring>
#include <cstdarg>

#include "lug_object.h"
#include "lug_vm.h"
#include "lug_gc.h"

namespace Luggage {

// Helper functions ------------------------------------

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
    object->hash = HashPointer(object);
    object->size = sizeof(T);
    
    vm.AddObject(object);

#if LUG_DEBUG_LOG_GC
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
 * Clamps the index and loops it back around given the
 * range of 0-numItems.
 */
static inline int ClampIndex(int index, int numItems) {
    ASSERT(numItems != 0, "numItems is zero");
    return ((index % numItems) + numItems) % numItems; 
}


static inline int ClampIndexWithBounds(int index, int numItems) {
    if (numItems == 0 || index >= numItems) return -1;
    return ClampIndex(index, numItems);
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


Obj* Obj::Copy() {
    return this;
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
 * 
 * This function expects that you pass in the characters, and then its length
 * WITHOUT the null terminator at the end.
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

/**
 * Creates an ObjString from a vector char buffer. This function
 * handles adding the null terminator at the end for you.
 */
ObjString* ObjString::FromBuffer(VMCore& vm, Vector<char>& buffer) {
    buffer.PushBack('\0');
    buffer.ShrinkToFit();

    std::size_t outCapacity, outCount;
    char* data = buffer.TakeOwnership(outCapacity, outCount);

    // FromAllocated expects the length of the string 
    // WITHOUT the null terminator
    return FromAllocated(vm, data, outCount - 1); 
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


ObjString* ObjString::Sub(int start, int end) {
    if (length == 0) return ObjString::FromLiteral(vm, "");

    int clampedStart = ClampIndex(start, length);
    int clampedEnd = ClampIndex(end, length);

    // "string"[5 to 3] will just return an empty string to keep consistent
    // with list behavior where start > end will return an empty list
    if (clampedStart > clampedEnd) return ObjString::FromLiteral(vm, "");

    ObjString* sub = ObjString::FromSource(
        vm,
        characters + clampedStart,
        clampedEnd - clampedStart + 1 // +1 to include the end character
    );
    
    return sub;
}


ObjString* ObjString::Concatenate(ObjString* other) {
    vm.stack.Push(this);
    vm.stack.Push(other);

    std::size_t totalLength = length + other->length;
    char* combinedChars = Memory::Allocate<char>(vm, totalLength + 1);

    std::memcpy(combinedChars, characters, length);
    std::memcpy(combinedChars + length, other->characters, other->length);
    combinedChars[totalLength] = '\0';

    ObjString* result = ObjString::FromAllocated(vm, combinedChars, totalLength);

    vm.stack.Pop();
    vm.stack.Pop();

    return result;
}


const char* ObjString::CharAt(int index) {
    int clampedIndex = ClampIndexWithBounds(index, length);
    if (clampedIndex == -1) throw vm.RuntimeError("string index '%d' out of range", index);

    return characters + clampedIndex;
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


void ObjFunction::MarkDefault() {
    defaults.PushBack(chunk.bytecode.Count());
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
    // We allocate this before ObjList*, because Memory::Allocate
    // can possibly trigger a GC. If wew allocate it after AllocateObject,
    // the ObjList* might get freed when we allocate the buffer.
    void* items = Memory::Allocate<ItemBuffer>(vm, 1);

    ObjList* list = AllocateObject<ObjList>(vm);
    list->hash = HashPointer(list);
    list->items = new (items) ItemBuffer(vm);
 
    return list;
}


ObjList::~ObjList() {
    items->refCount--;
    if (items->refCount == 0) Memory::Free<ItemBuffer>(vm, items);
}


void ObjList::Print() {
    std::printf("[ ");
    for (std::size_t i = 0; i < Count(); i++) {
        Value::Print(items->data[i]);
        if (i != Count() - 1) std::printf(", ");
    }

    std::printf(" ]");
}


ObjString* ObjList::ToString() {
    return ObjString::FromFormat(vm, "<list %p>", static_cast<void*>(this));
}


void ObjList::Blacken(GC& gc) {
    gc.MarkVector(items->data);
}


Obj* ObjList::Copy() {
    ObjList* copy = AllocateObject<ObjList>(vm);
    copy->hash = HashPointer(copy);
    copy->items = items;
    items->refCount++;

    return copy;
}


void ObjList::MakeUnique() {
    ItemBuffer* oldItems = items;
    oldItems->refCount--;

    ItemBuffer* fresh = Memory::Allocate<ItemBuffer>(vm, 1);

    /*
        Immediately set 'items' to item buffer, so that in case that
        when we're copying all the old items over and it triggers a 
        collection, the vm can reach them
    */
    items = new (fresh) ItemBuffer(vm);
    for (auto& oldItem : oldItems->data) {
        Value copy = oldItem.Copy();
        vm.stack.Push(copy);
        items->data.PushBack(copy);
        vm.stack.Pop();
    }
}


ObjList* ObjList::Slice(int start, int end) {
    if (Count() == 0) return ObjList::New(vm);

    int clampedStart = ClampIndex(start, Count());
    int clampedEnd = ClampIndex(end, Count());

    ObjList* sliced = ObjList::New(vm);
    vm.stack.Push(sliced);

    for (int i = clampedStart; i <= clampedEnd; i++) {
        Value item = Get(i)->Copy();
        vm.stack.Push(item);
        sliced->Append(item);
        vm.stack.Pop();
    }

    vm.stack.Pop(); // sliced
    return sliced;
}


void ObjList::Append(Value value) {
    if (items->refCount > 1) MakeUnique();

    items->data.PushBack(value);
}


Value* ObjList::Get(int index) {
    int clampedIndex = ClampIndexWithBounds(index, Count());
    if (clampedIndex == -1) return nullptr;
    /*
        If the getter returns a child that is a list, we want
        to make sure that the current list has a unique buffer. This is 
        so we don't have weird shared behavior when it comes to
        lists.
        
        box a <- [
            [1, 2, 3],
            [4, 5, 6]
        ]

        box b <- a
        a[0][2] <- 5 // a will split off from b and create a new unique matrix

        Without this, b and a will have the same contents.
    */
    if (items->refCount > 1 && items->data[clampedIndex].IsObj(ObjType::LIST)) {
        MakeUnique();
    }

    return &items->data[clampedIndex];
}


int ObjList::Count() const {
    return items->data.Count();
}


void ObjList::Set(int index, Value value) {
    int clampedIndex = ClampIndexWithBounds(index, Count());
    if (clampedIndex == -1) throw vm.RuntimeError("index '%d' out of range", index);

    if (items->refCount > 1) MakeUnique();

    items->data[clampedIndex] = value;
}


void ObjList::Insert(int index, Value value) {
    int clampedIndex = ClampIndexWithBounds(index, Count());
    if (clampedIndex == -1) throw vm.RuntimeError("index '%d' out of range", index);

    items->data.Insert(clampedIndex, value);
}


Value ObjList::Pop(int index) {
    int clampedIndex = ClampIndexWithBounds(index, Count());
    if (clampedIndex == -1 || Get(clampedIndex) == nullptr) {
        throw vm.RuntimeError("index '%d' out of range", index);
    }

    Value oldItem = *Get(clampedIndex);
    items->data.Erase(clampedIndex);
    return oldItem;
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


Obj* ObjDict::Copy() {
    ObjDict* copy = ObjDict::New(vm);
    vm.stack.Push(copy);
    for (auto& entry : entries) {
        Value key = entry.key.Copy();
        vm.stack.Push(key);
        Value value = entry.value.Copy();
        vm.stack.Push(value);

        copy->Set(key, value);
        vm.stack.Pop();
        vm.stack.Pop();
    }

    vm.stack.Pop(); // copy
    return copy;
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
    if (stringDunder != nullptr && stringDunder->IsObj(ObjType::CLOSURE)) {
        vm.stack.Push(*stringDunder);
        vm.stack.Push(this);

        // We will 'share' the thing.
        vm.Call(stringDunder->AsObj<ObjClosure>(), 1, 0b1);
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


Obj* ObjThing::Copy() {
    ObjThing* copy = ObjThing::New(vm);
    vm.stack.Push(copy);

    for (auto& attribute : attributes) {
        Value name = attribute.key.Copy();
        Value value = attribute.value.Copy();

        copy->Set(name.AsObj<ObjString>(), value);
    }
    
    vm.stack.Pop();
    return copy;
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
    newCategory->hash = HashPointer(newCategory);
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