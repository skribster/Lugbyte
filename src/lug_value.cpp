#include <cstdio>

#include "lug_memory.h"
#include "lug_value.h"

namespace Luggage {

// Value methods ------------------------------
void Value::Print(const Value& value) {
    switch (value.type) {
        case Value::Type::NUMBER: {
            std::printf("%g", value.AsNumber());
            break;
        }

        case Value::Type::BOOLEAN: {
            std::printf(value.AsBool() ? "true" : "false");
            break;
        }

        case Value::Type::NIL: {
            std::printf("nil");
        }

        default: {
            std::printf("UNKNOWN_VALUE");
            break;
        }
    }
}

// Value Array Methods ------------------------------

ValueArray::~ValueArray() {
    Memory::FreeArray<Value>(values, valueCapacity);
}


void ValueArray::Write(Value value) {
    if (valueCount >= valueCapacity) {
        std::size_t oldCapacity = valueCapacity;
        valueCapacity = Memory::GrowCapacity(oldCapacity);

        values = Memory::GrowArray<Value>(values, oldCapacity, valueCapacity);
    }

    values[valueCount] = value;
    valueCount++;
}

}