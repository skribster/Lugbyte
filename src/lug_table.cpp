#include <cstring>
#include <cstdio>

#include "lug_table.h"
#include "lug_memory.h"
#include "lug_object.h"

namespace Luggage {

Table::~Table() {
    Memory::FreeArray<Entry>(vm, entries, capacity);
}

/**
 * Sets a key-value pair in the table. Returns whether
 * if a new entry was added when setting the pair.
 */
bool Table::Set(Value key, Value value) {
    // If the table is filled to Table:MAX_LOAD
    // we will resize the capacity.
    if (count + 1 > capacity * Table::MAX_LOAD) { 
        std::size_t newCapacity = Memory::GrowCapacity(capacity);
        AdjustCapacity(newCapacity);
    }

    Entry* entry = FindEntry(key);
    bool isNewKey = entry->IsEmpty();

    // If we reached a new key and it's NOT a tombstone,
    // increment the count. Tombstones have no effect
    // on the count.
    if (isNewKey && !entry->key.IsTombstone()) count++;

    entry->key = key;
    entry->value = value;

    return isNewKey;
}


Value* Table::Get(Value key) const {
    if (count == 0) return nullptr;

    Entry* entry = FindEntry(key);
    if (entry->IsEmpty()) return nullptr;

    return &entry->value;
}


Value* Table::Get(const char* key) const {
    return Get(ObjString::CopyFrom(vm, key));
}

/**
 * Removes a key from the hash table.
 * Returns true if success. Returns false if no removal.
 */
bool Table::Delete(Value key) {
    /*
        In actual
        implementation, we replace the key with a 'tombstone'
        to preserve the order of linear probing, and then
        clear out every tombstone when we adjust the capacity.
    */

    // Can't remove anything in the first place
    // since there's nothing in the table???
    if (count == 0) return false;

    // Key doesn't exist in the table in the first place
    Entry* entry = FindEntry(key);
    if (entry->key.IsNil()) return false; 

    // Place a tombstone in the entry. Tombstones are
    // represented as an entry with a TOMBSTONE value.
    entry->key = Value::Tombstone();
    entry->value = Value::Nil();

    return true;
}


ObjString* Table::FindString(const char* chars, std::size_t length, std::uint32_t hash) const {
    if (count == 0) return nullptr;

    std::uint32_t index = hash % capacity;

    while (true) {
        Entry* entry = &entries[index];

        // Stop when we find a truly empty entry
        if (entry->key.IsNil()) return nullptr;
        if (entry->key.IsObj(ObjType::STRING)) {
            ObjString* string = entry->key.AsObj<ObjString>();
            if (
                string->length == length 
                && string->hash == hash
                && std::memcmp(string->characters, chars, length) == 0
            ) {
                // We found it!
                return string;
            }
        }
        
        index = (index + 1) % capacity;
    }
}


// TABLE PRIVATE METHODS ---------------------------------------
/**
 * Takes an Obj key and finds out which bucket the
 * entry should belong in.
 */
Entry* Table::FindEntry(Value& key) const {
    std::uint32_t index = key.GetHash() % capacity;
    
    /*
        We use a simple linear probing approach
        where if we come across an entry that's already
        full we start probing the ones next to it until
        we find one that has our key, or is an empty entry.
    */
   
    Entry* tombstone = nullptr;
    while (true) {
        Entry* entry = &entries[index];

        // We found the key!
        if (entry->key == key) return entry;

        // Did we pass a tombstone?
        if (entry->key.type == ValueType::TOMBSTONE) {
            if (tombstone == nullptr) tombstone = entry;
        // We fonud an empty entry.
        } else if (entry->key.IsNil() && entry->value.IsNil()) {
            // If we passed by a tombstone on our way, we will
            // return that instead of the empty entry.
            return tombstone != nullptr ? tombstone : entry;
        }

        // Probe to the right. We use modulo here to prevent
        // the index from overgrowing the capacity, looping
        // the index right back to the left
        index = (index + 1) % capacity;
    }
}


void Table::AdjustCapacity(std::size_t newCapacity) {
    // To adjust the capacity of the table, we will first
    // allocate a brand new table and clear out all its garbage contents.
    Entry* newEntries = Memory::Allocate<Entry>(vm, newCapacity);
    for (std::size_t i = 0; i < newCapacity; i++) {
        newEntries[i].key = Value::Nil();
        newEntries[i].value = Value::Nil();
    }

    Entry* oldEntries = entries;
    std::size_t oldCapacity = capacity;

    entries = newEntries;
    capacity = newCapacity;

    /*
        Then, we will put all of the values of the old
        table into the new table.

        Since we use the capacity for choosing the bucket for each entry 
        (recall hash % capacity) there's a chance that after resizing,
        an entry might not point to the same entry as before we resized it.
    !   The easiest way is to rebuild the table from scratch.
    */

    // When we rebuild the array, we don't include
    // the tombstones. This means we also have to recalculate
    // the count of the table to account for all the removed
    // entries.
    count = 0;
    for (std::size_t i = 0; i < oldCapacity; i++) {
        Entry* entry = &oldEntries[i];
        if (entry->key.IsNil() || entry->key.IsTombstone()) continue;

        Entry* destination = FindEntry(entry->key);
        destination->key = entry->key;
        destination->value = entry->value;
        count++;
    }

    Memory::FreeArray<Entry>(vm, oldEntries, oldCapacity);
}


}
