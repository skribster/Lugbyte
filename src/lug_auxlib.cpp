#include "lug_auxlib.h"


namespace Luggage::Lib {

[[noreturn]] void ArgcError(VMCore& vm, std::size_t expected, std::size_t got) {
    throw vm.RuntimeError("expected %zu argument(s), got %zu", expected, got);
}


[[noreturn]] void TypeError(VMCore& vm, const char* expected, const char* got) {
    throw vm.RuntimeError("'%s' expected, got '%s'", expected, got);
}

    
void CheckArgc(VMCore& vm, std::size_t argc, std::size_t expected) {
    if (argc != expected) {
        ArgcError(vm, expected, argc);
    }
};

    
void CheckType(VMCore& vm, Value& value, ValueType expected) {
    if (value.type != expected) {
        TypeError(vm, Value::StringType(expected), value.StringType());
    }
}


void CheckType(VMCore& vm, Value& value, ObjType expected) {
    if (!value.IsObj() || value.AsObj()->type != expected) {
        TypeError(vm, Value::StringType(expected), value.StringType());
    }
}

}