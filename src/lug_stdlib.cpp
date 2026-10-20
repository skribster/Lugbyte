#include "lug_stdlib.h"


namespace Luggage::Lib {

ObjThing* NewLib(VMCore& vm, const char* libraryName) {
    ObjThing* library = ObjThing::New(vm);
    ObjString* name = ObjString::FromLiteral(vm, libraryName);
    vm.globals.Set(name, library);

    return library;
};

}