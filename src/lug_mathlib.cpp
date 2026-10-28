#include <cmath>
#include <limits>

#include "lug_stdlib.h"
#include "lug_auxlib.h"
#include "lug_value.h"

namespace Luggage::Lib {

// Library implementation functions -----------------------------

static Value Abs(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ValueType::NUMBER);

    double number = args[0].AsNumber();
    return std::fabs(number);
}


static Value Sqrt(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ValueType::NUMBER);

    double number = args[0].AsNumber();
    if (number < 0) {
        vm.RuntimeError("can't square root a negative number");
    }

    return std::sqrt(number);
}


static Value Clamp(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 3);
    CheckType(vm, args[0], ValueType::NUMBER);
    CheckType(vm, args[1], ValueType::NUMBER);
    CheckType(vm, args[2], ValueType::NUMBER);

    double value = args[0].AsNumber();
    double lo = args[1].AsNumber();
    double hi = args[2].AsNumber();

    if (lo > hi) {
        vm.RuntimeError("lower limit of clamp is greater than the upper limit");
    }

    if (value < lo) value = lo;
    if (value > hi) value = hi;

    return value;
}


static Value Sin(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ValueType::NUMBER);

    double number = args[0].AsNumber();

    return std::sin(number);
}


static Value Cos(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ValueType::NUMBER);
    
    double number = args[0].AsNumber();
    
    return std::cos(number);
}


static Value Tan(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ValueType::NUMBER);
    
    double number = args[0].AsNumber();
    
    return std::tan(number);
}


static Value Asin(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ValueType::NUMBER);
    
    // The first argument is the sine of an angle.
    double sine = args[0].AsNumber();
    return std::asin(sine);
}



static Value Acos(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ValueType::NUMBER);
    
    // The first argument is the cosine of an angle.
    double cosine = args[0].AsNumber();
    return std::acos(cosine);
}


static Value Atan(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ValueType::NUMBER);
    
    // The first argument is the tangent of an angle.
    double cosine = args[0].AsNumber();
    return std::atan(cosine);
}


static Value Atan2(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 2);
    CheckType(vm, args[0], ValueType::NUMBER);
    CheckType(vm, args[1], ValueType::NUMBER);
    
    // The first argument is the tangent of an angle.
    double x = args[0].AsNumber();
    double y = args[0].AsNumber();
    
    return std::atan2(x, y);
}


static Value Sign(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 2);
    CheckType(vm, args[0], ValueType::NUMBER);

    double number = args[0].AsNumber();

    if (number < 0) return -1;
    if (number == 0) return 0;
    if (number > 0) return 1;

    return 0;
}

// Library out ------------------------------

static constexpr NativeEntry mathLib[] = {
    { "abs",        &Abs },
    { "sqrt",       &Sqrt },
    { "clamp",      &Clamp },
    { "sin",        &Sin },
    { "cos",        &Cos },
    { "tan",        &Tan },
    { "asin",       &Asin },
    { "acos",       &Acos },
    { "atan",       &Atan },
    { "atan2",      &Atan2 },
    { "sign",       &Sign }
};

// Function declaration of defined in lug_stdlib.h
void OpenMath(VMCore& vm) { 
    ObjThing* library = NewLib(vm, "math");
    DefineLib(vm, library, mathLib); 
}

}