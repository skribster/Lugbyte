#pragma once

#include "lug_common.h"
#include "lug_value.h"

namespace Luggage {

class VMCore;
struct ObjString;

struct Entry {
    Value key{ Value::Nil() };
    Value value{ Value::Nil() };
};


struct Table {
public:
    explicit Table(VMCore& vm) : vm(vm) {}
    ~Table();

    bool Set(Value key, Value value);
    Value* Get(Value key) const;
    Value* Get(const char* keyName) const;
    bool Delete(Value key);
    
    ObjString* FindString(
        const char* characters, 
        std::size_t length, 
        std::uint32_t hash
    ) const;

    VMCore& vm;
    std::size_t count{ 0 };
    std::size_t capacity{ 0 };
    Entry* entries{ nullptr };

    static constexpr double MAX_LOAD{ 0.75 };

private:
    Entry* FindEntry(Value& key) const;
    void AdjustCapacity(std::size_t newCapacity);
};

}