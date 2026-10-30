#include "lug_vm.h"
#include "lug_gc.h"
#include "lug_common.h"
#include "lug_memory.h"

#include "lug_compiler.h"

namespace Luggage {

GC::~GC() {
    
}


// Public methods -----------------------------------

void GC::Collect() {

#ifdef LUG_DEBUG_LOG_GC
    std::printf("--gc begin\n");
    std::size_t before = bytesAllocated;
#endif

    vm.MarkRoots();
    TraceReferences();
    vm.strings.DeleteWhite();
    Sweep();

    nextGC = bytesAllocated * HEAP_GROW_FACTOR;

#ifdef LUG_DEBUG_LOG_GC
    std::printf("--gc end\n");
    std::printf(
        "collected %zu bytes (from %zu to %zu) next at %zu\n",
        before - bytesAllocated, before, bytesAllocated, nextGC
    );
#endif

}

// Mark methods -----------------------------------

void GC::MarkValue(Value& value) {
    if (value.IsObj()) MarkObject(value.AsObj());
}


void GC::MarkObject(Obj* obj) {
    if (obj == nullptr) return;

    /*
        We don't want to re-mark an object because it
        might cause an infinite loop when we're 
        blackening the object later on if they have 
        references that share each other 
    */
    if (obj->isMarked) return;

#ifdef LUG_DEBUG_LOG_GC
    std::printf("%p mark ", static_cast<void*>(obj));
    Value::Print(obj);
    std::printf("\n");
#endif

    obj->isMarked = true;

    // We'll add the object to the work list by adding
    // it to the gray stack.
    if (grayCount >= grayCapacity) {
        grayCapacity = Memory::GrowCapacity(grayCapacity);
        
        /*
            We don't use the Memory::Reallocate wrapper for this,
            because Memory::Reallocate touches the garbage collector.
            This gray stack does not, because we don't want growing
            the gray stack during a garbage collection to itself cause
            another garbage collection.
        */
        grayStack = static_cast<Obj**>(std::realloc(grayStack, sizeof(Obj*) * grayCapacity));
    
        if (grayStack == nullptr) throw std::bad_alloc(); 
    }

    grayStack[grayCount++] = obj;
}


void GC::MarkTable(Table& table) {
    for (auto& entry : table) {
        MarkValue(entry.key);
        MarkValue(entry.value);
    }
}


void GC::MarkVector(Vector<Value>& vector) {
    for (auto& item : vector) {
        MarkValue(item);
    }
}


void GC::AddObject(Obj* obj) {
    obj->next = objects;
    objects = obj;
}

// Trace methods -----------------------------------

/**
 * This function traces all of the current gray 
 * objects and blackens each one.
 */
void GC::TraceReferences() {
    while (grayCount > 0) {
        Obj* object = grayStack[--grayCount];
        BlackenObject(object);
    }
}

/**
 * Blackens a given object. An object is 'blackened'
 * when it's marked and it's not in the gray stack
 * anymore. Blackening an object also means traversing
 * its object references and marking those as well.
 */
void GC::BlackenObject(Obj* obj) {
#ifdef LUG_DEBUG_LOG_GC
    std::printf("%p blacken ", static_cast<void*>(obj));
    Value::Print(obj);
    std::printf("\n");
#endif

    obj->Blacken(*this);
}

// Free methods -----------------------------------

void GC::FreeObject(Obj* obj) {
#ifdef LUG_DEBUG_LOG_GC
    std::printf("%p free ", static_cast<void*>(obj));
    Value::Print(obj);
    std::printf("\n");
#endif

    // Store the size before we call the destructor
    // since the destructor might modify the size or other members of the object...
    std::size_t size = obj->size; 
    obj->~Obj();
    Memory::Reallocate(vm, obj, size, 0);
}


void GC::FreeObjects() {
    Obj* object = objects;
    while (object != nullptr) {
        Obj* next = object->next;
        FreeObject(object);
        object = next;
    }

    std::free(grayStack);
}


void GC::Sweep() {
    Obj* previous = nullptr;
    Obj* object = objects;

    while (object != nullptr) {
        // We don't sweep any object that
        // is marked (black in this case)
        if (object->isMarked) {
            // Set the marked flag back to false
            // to prepare it for the next collection
            // cycle.
            object->isMarked = false;

            previous = object;
            object = object->next;
            continue;
        }

        // When an object is white, we first
        // unlink it from the object list,
        // and free it.
        Obj* white = object;
        object = object->next;
        
        // Unlink the object. Set the previous's
        // object to the object right after the 
        // white object.
        if (previous != nullptr) {
            previous->next = object;
        } 

        // If there is no previous,
        // just set the start of the linked list
        // to the object.
        else {
            objects = object;
        }

        FreeObject(white);
    }
}


}