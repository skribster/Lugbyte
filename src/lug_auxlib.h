#pragma once

#include "lug_common.h"
#include "lug_vm.h"

namespace Luggage::Lib {

[[noreturn]] void ArgcError(VMCore& vm, std::size_t expected, std::size_t got);
[[noreturn]] void TypeError(VMCore& vm, const char* expected, const char* got);

void CheckArgc(VMCore& vm, std::size_t argc, std::size_t expected);
void CheckType(VMCore& vm, Value& value, ValueType expected);
void CheckType(VMCore& vm, Value& value, ObjType expected);

}