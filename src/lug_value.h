#pragma once


namespace Luggage {

/**
 * Represents a single Luggage dynamic value.
 * 
 *
 * We store a value as a struct that holds the current
 * type of the value, and a union that's the actual
 * value of the... well, value.
 * 
 * Visualized
 * ----------------------------------
 * (Keep in mind that the extra padding is due to
 * struct alignment, where each item is aligned
 * in byte multiples of the largest item in size)
 * [****] [     ] [********]
 *  ^^^^   ^^^^^   ^^^^^^^^
 *  Type  Padding    'as'
 *                 ^^^^^^^^
 *                   number 
 *                 ^
 *              boolean
 */
struct Value {
public:
    enum class Type {
        BOOLEAN,
        NUMBER,
        NIL,
        OBJ,
    };

    Value() : type(Type::NIL) { as.number = 0; }

    bool operator==(const Value& other) const;
    
    Value(double number) : type(Type::NUMBER)       { as.number = number; }
    Value(int number) : type(Type::NUMBER)          { as.number = static_cast<double>(number); }
    Value(bool boolean) : type(Type::BOOLEAN)       { as.boolean = boolean; }

    // Named singleton constructors
    inline static Value Nil() { return Value(Type::NIL); }

    // Casting methods for converting a struct Value to a native C++ value
    inline double AsNumber() const  { return as.number; }
    inline bool AsBool() const      { return as.boolean; }

    // Type-checking methods for checking the type of a struct Value
    inline bool IsNumber() const    { return type == Type::NUMBER; }
    inline bool IsBool() const      { return type == Type::BOOLEAN; }
    inline bool IsNil() const       { return type == Type::NIL; }

    static void Print(const Value& value);

    Type type;
    
private:
    // Tag-only constructor used ONLY internally for building a singleton type
    explicit Value(Type type) : type(type) { as.number = 0; }

    union {
        double number;
        bool boolean;
    } as;
};

class VMCore;
struct ValueArray {
    ValueArray(VMCore& vm) : vm(vm) {}
    ~ValueArray();

    void Write(Value value);    

    std::size_t valueCapacity{ 0 };
    std::size_t valueCount{ 0 };
    Value* values{ nullptr };

    VMCore& vm;
};

}