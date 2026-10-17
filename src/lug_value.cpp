#include <cstdio>
#include <cstring>

#include "lug_memory.h"
#include "lug_value.h"
#include "lug_object.h"

namespace Luggage {

// Helper functions ------------------------------
/**
 * Hashes a double into a uint32_t
 */
static std::uint32_t HashDouble(double value) {
    // Handle -0.0 and 0.0 to hash them equally.
    // For some reason when you hash them apart,
    // they will get different hashes EVEN THOUGH
    // they have the same value!!!! :(
    if (value == 0.0) value = 0.0;

    std::uint64_t raw;
    // Copy the bits of the double into the raw std::uint64_t
    std::memcpy(&raw, &value, sizeof(raw));

    // Fold the std::uint64_t into a std::uint32_t
    std::uint32_t hash = static_cast<std::uint32_t>(raw ^ (raw >> 32));
    
    return hash;
}

// Value methods ------------------------------

bool Value::IsObj(ObjType type) const {
    return IsObj() && AsObj()->type == type;
}


bool Value::operator==(const Value& other) const {
    if (type != other.type) return false;

    switch (type) {
        case ValueType::BOOLEAN: {
            return AsBool() == other.AsBool(); 
        }

        case ValueType::NUMBER: {
            return AsNumber() == other.AsNumber();
        }

        case ValueType::OBJ: {
            return AsObj() == other.AsObj();
        }

        case ValueType::NIL: {
            return true;
        }

        default: UNREACHABLE();
    }
}


void Value::Print(const Value& value) {
    switch (value.type) {
        case ValueType::NUMBER: {
            std::printf("%g", value.AsNumber());
            break;
        }

        case ValueType::BOOLEAN: {
            std::printf(value.AsBool() ? "true" : "false");
            break;
        }

        case ValueType::NIL: {
            std::printf("nil");
            break;
        }

        case ValueType::OBJ: {
            value.AsObj()->Print();
            break;
        }

        default: {
            std::printf("UNKNOWN_VALUE");
            break;
        }
    }
}


const char* Value::StringType() const {
    if (IsObj()) {
        return Value::StringType(AsObj()->type);
    } else {
        return Value::StringType(type);
    }
}


const char* Value::StringType(ValueType type) {
    switch (type) {
        case ValueType::BOOLEAN:    return "boolean";
        case ValueType::NIL:        return "nil";
        case ValueType::NUMBER:     return "number";

        default: {
            UNREACHABLE();
            return "unknown";
        }
    }
}


const char* Value::StringType(ObjType type) {
    return Obj::StringType(type);
}


std::uint32_t Value::GetHash() const {
    switch (type) {
        case ValueType::BOOLEAN:    return AsBool() ? 3 : 5;
        case ValueType::NIL:        return 7;
        case ValueType::NUMBER:     return HashDouble(AsNumber());
        case ValueType::OBJ:        return AsObj()->hash;
        case ValueType::TOMBSTONE:  return 0;

        default: UNREACHABLE();
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