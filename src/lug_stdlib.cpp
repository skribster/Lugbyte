#include "lug_stdlib.h"


namespace Luggage::Lib {

ObjThing* NewLib(VMCore& vm, const char* libraryName) {
    ObjThing* library = ObjThing::New(vm);
    ObjString* name = ObjString::CopyFrom(vm, libraryName, std::strlen(libraryName));
    vm.globals.Set(name, library);

    return library;
};

}