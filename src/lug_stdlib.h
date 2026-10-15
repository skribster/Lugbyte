#pragma once

#include "lug_common.h"
#include "lug_vm.h"
#include "lug_object.h"

namespace Luggage::Lib { 

struct NativeEntry {
    const char* name;
    NativeFn function;
};

/**
 * Helper function for defining all the native functions of
 * a given library and adding them inside the VM's environment.
 * 
 * If the given library name isn't specified (nullptr), all of the
 * entries will be defined in the global scope. Otherwise, all entries
 * will be stuffed into one `thing` global.
 */
template<std::size_t N>
void DefineLib(VMCore& vm, const char* libraryName, const NativeEntry (&entries)[N]) {
    if (!libraryName) {
        for (auto& entry : entries) {
            vm.DefineNative(entry.name, entry.function);
        }

        return;
    }

    // Otherwise...

    ObjString* nameValue = ObjString::CopyFrom(vm, libraryName, std::strlen(libraryName));
    vm.Push(nameValue);

    // Check if we already defined the library before in the globals.
    if (vm.globals.Get(nameValue) == nullptr) {
        ObjThing* library = ObjThing::New(vm);
        vm.Push(library);
        vm.globals.Set(nameValue, library);
        vm.Pop(); // Library;
    }

    ObjThing* library = vm.globals.Get(nameValue)->AsObj<ObjThing>();

    vm.Pop(); // nameValue

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

}