#pragma once

#include "lug_common.h"
#include "lug_vm.h"

namespace Luggage::Lib {

void CheckArgc(VMCore& vm, std::size_t argv, std::size_t expected);
void CheckType(VMCore& vm, Value& value, ValueType expected);
void CheckType(VMCore& vm, Value& value, ObjType expected);

}