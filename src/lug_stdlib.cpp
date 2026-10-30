#include "lug_stdlib.h"


namespace Luggage::Lib {

ObjThing* NewLib(VMCore& vm, const char* libraryName) {
    ObjThing* library = ObjThing::New(vm);
    vm.stack.Push(library);

    ObjString* name = ObjString::FromLiteral(vm, libraryName);
    vm.stack.Push(name);

    vm.builtins.Set(name, library);

    vm.stack.Pop();
    vm.stack.Pop();

    return library;
};

}