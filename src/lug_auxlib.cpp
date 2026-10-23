#include "lug_auxlib.h"


namespace Luggage::Lib {


// Helper functions  ---------------------------------


// API Parameter Checking ---------------------------------
    
void CheckArgc(VMCore& vm, std::size_t argc, std::size_t expected) {
    if (argc != expected) {
        vm.RuntimeError("Expected %zu argument(s), got %zu", argc, expected);
    }
};

    
void CheckType(VMCore& vm, Value& value, ValueType expected) {
    if (value.type != expected) {
        vm.RuntimeError("'%s' expected, got '%s'", Value::StringType(expected), value.StringType());
    }
}


void CheckType(VMCore& vm, Value& value, ObjType expected) {
    if (!value.IsObj() || value.AsObj()->type != expected) {
        vm.RuntimeError("'%s' expected, got '%s'", Value::StringType(expected), value.StringType());
    }
}

}