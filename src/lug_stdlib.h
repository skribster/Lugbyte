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
 */
template<std::size_t N>
void DefineNatives(VMCore& vm, const NativeEntry (&entries)[N]) {
    for (auto& entry : entries) {
        vm.DefineNative(entry.name, entry.function);
    }
}

// Library open functions -------------------------

extern void OpenBase(VMCore& vm);

}