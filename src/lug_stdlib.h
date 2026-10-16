#pragma once

#include "lug_common.h"
#include "lug_vm.h"
#include "lug_object.h"

namespace Luggage::Lib { 

struct NativeEntry {
    const char* name;
    NativeFn function;
};


ObjThing* NewLib(VMCore& vm, const char* libraryName);

/**
 * Helper function for defining all the native functions of
 * a given library and adding them inside the VM's environment.
 * 
 * If the given library name isn't specified (nullptr), all of the
 * entries will be defined in the global scope. Otherwise, all entries
 * will be stuffed into one `thing` global.
 */
template<std::size_t N>
void DefineLib(VMCore& vm, ObjThing* library, const NativeEntry (&entries)[N]) {
    if (library == nullptr) {
        for (auto& entry : entries) {
            vm.DefineNative(entry.name, entry.function);
        }

        return;
    }

    for (const NativeEntry& entry : entries) {
        ObjString* entryName = ObjString::CopyFrom(vm, entry.name, std::strlen(entry.name));
        ObjNative* entryFunction = ObjNative::New(vm, entry.name, entry.function);
        vm.Push(entryName);
        vm.Push(entryFunction);

        library->Set(entryName, entryFunction);

        vm.Pop();
        vm.Pop();
    }
}

// Library open functions -------------------------

extern void OpenBase(VMCore& vm);
extern void OpenMath(VMCore& vm);
extern void OpenString(VMCore& vm);
extern void OpenCollection(VMCore& vm);

}