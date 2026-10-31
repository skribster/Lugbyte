#pragma once

#include <cmath>
#include <type_traits>
#include "lug_vector.h"

namespace Luggage {

enum class ValueType {
    NUMBER,
    BOOLEAN,
    NIL,
    OBJ,

    // Not to be used directly.
    TOMBSTONE
};


struct VMCore;
struct Obj;
struct ObjString;
enum class ObjType;

/**
 * Represents a single Luggage dynamic value.
 */
struct Value {
public:
    Value() : type(ValueType::NIL) { as.number = 0; }

    bool operator==(const Value& other) const;
    
    Value(double number) : type(ValueType::NUMBER)          { as.number = number; }
    Value(int number) : type(ValueType::NUMBER)             { as.number = static_cast<double>(number); }
    Value(bool boolean) : type(ValueType::BOOLEAN)          { as.boolean = boolean; }
    Value(Obj* obj) : type(ValueType::OBJ)                  { as.obj = static_cast<Obj*>(obj); }

    /*
        This makes it so that Value can accept object pointers (including
        pointers whose class inherits Obj, aka ObjString, ObjFunction, etc.)
        but not any other pointer.
    */
    template <typename T, typename = std::enable_if_t<!std::is_base_of_v<Obj, T>>>
    Value(T* anyOtherPointer) = delete;

    // Named singleton constructors
    inline static Value Nil()           { return Value(ValueType::NIL); }
    inline static Value Tombstone()     { return Value(ValueType::TOMBSTONE); }

    // Casting methods for converting a struct Value to a native C++ value
    inline double AsNumber() const      { return as.number; }
    inline int AsInteger() const        { return static_cast<int>(AsNumber()); }
    inline bool AsBool() const          { return as.boolean; }
    inline Obj* AsObj() const           { return as.obj; }

    template<typename T> 
    T* AsObj() const { return static_cast<T*>(AsObj()); }

    // Type-checking methods for checking the type of a struct Value
    inline bool IsNumber() const        { return type == ValueType::NUMBER; }
    inline bool IsNan() const           { return IsNumber() && std::isnan(AsNumber()); }
    inline bool IsInteger() const       { return IsNumber() && static_cast<int>(AsNumber()) == AsNumber(); }

    inline bool IsBool() const          { return type == ValueType::BOOLEAN; }
    inline bool IsNil() const           { return type == ValueType::NIL; }
    inline bool IsObj() const           { return type == ValueType::OBJ; }
    inline bool IsTombstone() const     { return type == ValueType::TOMBSTONE; }
    
    bool IsObj(ObjType type) const;

    // I like the convention of passing in the value to Print()
    // because it looks better..........
    static void Print(const Value& value);
    static const char* StringType(ValueType type);
    static const char* StringType(ObjType type);

    const char* StringType() const;
    std::uint32_t GetHash() const;
    ObjString* ToString(VMCore& vm) const;
    Value Copy();

    ValueType type;
    
private:
    // Tag-only constructor used ONLY internally for building a singleton type
    explicit Value(ValueType type) : type(type) { as.number = 0; }

    /*
        We store a value as a struct that holds the current
        type of the value, and a union that's the actual
        value of the... well, value.
        
        Visualized
        ----------------------------------
        (Keep in mind that the extra padding is due to
        struct alignment, where each item is aligned
        in byte multiples of the largest item in size)

        [****] [     ] [********]
         ^^^^   ^^^^^   ^^^^^^^^
         Type  Padding    'as'
                        ^^^^^^^^
                          number 
                        ^
                     boolean

    */
    union {
        double number;
        bool boolean;
        Obj* obj;
    } as;
};


}