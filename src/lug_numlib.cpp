#include <cmath>
#include <limits>
#include <charconv>

#include "lug_stdlib.h"
#include "lug_auxlib.h"
#include "lug_value.h"


namespace Luggage::Lib {

constexpr double PI = 3.14159265358979323846;
constexpr double RADIANS_PER_DEGREE = PI / 180.0;
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

// Library implementation functions -----------------------------

static Value Radians(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ValueType::NUMBER);

    double degrees = args[0].AsNumber();

    return degrees * RADIANS_PER_DEGREE;
}


static Value Degrees(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    CheckType(vm, args[0], ValueType::NUMBER);
    
    double radians = args[0].AsNumber();
    
    return radians / RADIANS_PER_DEGREE;
}


static Value Round(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);

    double number = args[0].AsNumber();

    return std::round(number);
}


static Value Floor(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);

    double number = args[0].AsNumber();

    return std::floor(number);
}


static Value Ceiling(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);

    double number = args[0].AsNumber();

    return std::ceil(number);
}


static Value IsInteger(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);
    return args[0].IsInteger();
}


static Value __Call__(VMCore& vm, Value* args, std::size_t argCount) {
    CheckArgc(vm, argCount, 1);

    Value value = args[0];

    if (value.IsBool()) {
        return value.AsBool() ? 1 : 0;
    }
    
    if (value.IsObj(ObjType::STRING)) {
        ObjString* string = value.AsObj<ObjString>();
        char* characters = string->characters;
        std::size_t length = string->length;

        double value = 0.0;

        // from_chars operates on a raw character buffer range
        auto [ptr, errorCode] = std::from_chars(characters, characters + length, value);

        // Successful path
        if (errorCode == std::errc()) return value;
    }

    return Value::Nil();
}

// Library out ------------------------------

static constexpr NativeEntry numberLib[] = {
    { "radians",    &Radians },
    { "degrees",    &Degrees },
    { "round",      &Round },
    { "floor",      &Floor },
    { "ceiling",    &Ceiling },
    { "isinteger",  &IsInteger },
    { "__call__",   &__Call__ },
};

// Function declaration of defined in lug_stdlib.h
void OpenNumber(VMCore& vm) { 
    ObjThing* library = NewLib(vm, "number");

    // Number constants.
    library->Set("pi", PI);
    library->Set("tau", PI * 2);
    library->Set("huge", HUGE_VAL);
    library->Set("nan", NaN);

    DefineLib(vm, library, numberLib); 
}




}