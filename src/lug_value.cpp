#include <cstdio>

#include "lug_memory.h"
#include "lug_value.h"

namespace Luggage {

// Value methods ------------------------------
bool Value::operator==(const Value& other) const {
    if (type != other.type) return false;

    switch (type) {
        case Value::Type::BOOLEAN: {
            return AsBool() == other.AsBool(); 
        }

        case Value::Type::NUMBER: {
            return AsNumber() == other.AsNumber();
        }

        case Value::Type::NIL: {
            return true;
        }

        default: UNREACHABLE();
    }
}


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
            break;
        }

        default: {
            std::printf("UNKNOWN_VALUE");
            break;
        }
    }
}

// Value Array Methods ------------------------------

ValueArray::~ValueArray() {
    Memory::FreeArray<Value>(vm, values, valueCapacity);
}


void ValueArray::Write(Value value) {
    if (valueCount >= valueCapacity) {
        std::size_t oldCapacity = valueCapacity;
        valueCapacity = Memory::GrowCapacity(oldCapacity);

        values = Memory::GrowArray<Value>(vm, values, oldCapacity, valueCapacity);
    }

    values[valueCount] = value;
    valueCount++;
}

}