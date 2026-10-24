#pragma once

#include "lug_common.h"
#include "lug_value.h"

namespace Luggage {

class VMCore;
struct ObjString;

struct Entry {
    Value key{ Value::Nil() };
    Value value{ Value::Nil() };

    /**
     * Returns whether an entry is 'empty'. An entry is empty when the key 
     * is not a TOMBSTONE and it's not nil. Any other case, we can consider
     * it as occupied.
     */
    inline bool IsEmpty() const { return key.IsTombstone() || key.IsNil(); }    
};

class TableIterator {
public:
    TableIterator(Entry* current, Entry* end)
        : current(current), end(end) {
        SkipEmpty(); // land on a live entry (or end)
    }

    // Prefix ++
    TableIterator& operator++() {
        ++current;
        SkipEmpty();
        return *this;
    }

    // Postfix ++
    TableIterator operator++(int) {
        TableIterator previous = *this;
        ++(*this);
        return previous;
    }

    Entry& operator*() const { return *current; }
    Entry* operator->() const { return current; }

    bool operator==(const TableIterator& other) const {
        return current == other.current;
    }
    bool operator!=(const TableIterator& other) const {
        return current != other.current;
    }

private:
    // Advance until we hit a live entry or the end.
    // Does nothing if already on a live entry.
    void SkipEmpty() {
        while (current != end && current->IsEmpty()) {
            ++current;
        }
    }

    Entry* current;
    Entry* end;
};


struct Table {
public:
    using Iterator = TableIterator;

    explicit Table(VMCore& vm) : vm(vm) {}
    ~Table();

    bool Set(Value key, Value value);
    Value* Get(const Value& key) const;
    Value* Get(const char* keyName) const;
    bool Delete(Value key);
    void DeleteWhite();
    
    ObjString* FindString(
        const char* characters, 
        std::size_t length, 
        std::uint32_t hash
    ) const;


    // Iterator methods ----------

    Iterator begin() {
        return Iterator(entries, entries + capacity);
    }

    Iterator end() {
        Entry* last = entries + capacity;
        return Iterator(last, last);
    }

    // Operator overloads --------

    const Entry& operator[](std::size_t index) const {
        return entries[index];
    }

    Entry& operator[](std::size_t index) {
        return entries[index];
    }


    // Member variables ----------

    VMCore& vm;

    // Do not replace with Luggage::Vector because this isn't
    // a dynamic array. It's a hashmap. I'm stupid and I tried doing
    // this and it did not work because of the logic in AdjustCapacity().
    std::size_t count{ 0 };
    std::size_t capacity{ 0 };
    Entry* entries{ nullptr };

    static constexpr double MAX_LOAD{ 0.75 };

private:
    Entry* FindEntry(const Value& key) const;
    void AdjustCapacity(std::size_t newCapacity);
};

}