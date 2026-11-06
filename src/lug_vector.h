#pragma once

#include <utility>
#include <type_traits>
#include <cstring>

#include "lug_common.h"
#include "lug_memory.h"

namespace Luggage {

class VMCore;

template<typename Vector>
class VectorIterator {
public:
    using ValueType = typename Vector::ValueType;
    
    VectorIterator(ValueType* pointer) : pointer(pointer) {

    }

    // Prefix ++
    VectorIterator& operator++() {
        pointer++;
        return *this;
    }

    // Postfix ++
    VectorIterator operator++(int) {
        VectorIterator iterator = *this;
        ++(*this);
        return iterator;
    }

    // Prefix --
    VectorIterator& operator--() {
        pointer--;
        return *this;
    }

    // Postfix --
    VectorIterator operator--(int) {
        VectorIterator iterator = *this;
        --(*this);
        return iterator;
    }

    ValueType& operator[](int index) { return *(pointer + index); }
    ValueType* operator->() { return pointer; }
    ValueType& operator*() { return *pointer; }

    bool operator==(const VectorIterator& other) const {
        return pointer == other.pointer;
    }


    bool operator!=(const VectorIterator& other) const {
        return !(pointer == other.pointer);
    }

private:
    ValueType* pointer;

};


/**
 * A Luggage dynamic array implementation.
 */
template<typename T>
class Vector {
public:
    using ValueType = T;
    using Iterator = VectorIterator<Vector<T>>;

public:

    // Constructors/destructors ----------------

    /*
        Copy constructor/assignment will lead to weird behavior 
        like double frees. For instance if we allowed:

        Vector<std::uint8_t> b = a;
        
        When a & b go out of scope, they will try freeing 
        the same memory address, leading to a double free!!!!
    */
    Vector(const Vector& other) = delete;
    Vector& operator=(const Vector& other) = delete;

    /*
        The move operator steals the resource instead of copying
        it. The new object takes the pointer, count, and capacity,
        then the source is reset to an empty state.
    */
    Vector(Vector&& other) noexcept
        : vm(other.vm), data(other.data), count(other.count), capacity(other.capacity) 
    {
        other.data = nullptr; 
        other.count = 0;
        other.capacity = 0;
    }


    /*
        Same for the move assignment operator.
    */
    Vector& operator=(Vector&& other) noexcept {
        // Guard against self-assignment
        if (this == &other) return *this;

        /*
            Say we move-assign b to a, where a owns its own buffer.

            Vector<int> a;
            a = std::move(b); // just casts to rvalue, same as (Vector&&)b

            In the constructor, we are overwriting a.data with b.data.

            a.data = b.data
            ^^^^^^
            The memory block a.data was pointing to never got freed, so this
            causes a memory leak. To prevent this, we first free a.data BEFORE
            we set it to b.data. 

            In addition, we also call Clear(), which will call each item's 
            destructor to be on the safe size.
        */
        Clear();
        Memory::FreeArray(vm, data, capacity);

        ASSERT(&vm == &other.vm, "can't move-assign vectors with different VMs");
        data = other.data;
        count = other.count;
        capacity = other.capacity;

        other.data = nullptr;
        other.count = 0;
        other.capacity = 0;

        return *this;
    }

    explicit Vector(VMCore& vm) : vm(vm) {
        static_assert(std::is_trivially_copyable_v<T>, "Vector uses realloc");
    }
    
    ~Vector() { 
        Clear(); 
        Memory::FreeArray<T>(vm, data, capacity); 
    }

    // Methods ----------------------
    
    void PushBack(const T& value) {
        /*
            If we pass in a reference to the table in PushBack, such as...
            a.PushBack(a[0]), there's a chance that before we actually set
            data[count] = copy, the table reallocates. If Reallocate moves
            the memory address of the data, a[0] could point to the old 
            block.
            
            To fix this, we instead make a snapshot of the value before
            we actually reallocate.
        */
        T copy = value;

        if (count >= capacity) Reallocate(Memory::GrowCapacity(capacity));

        data[count] = copy;
        count++;
    }

    void PushBack(T&& value) {
        T copy = std::move(value);
        if (count >= capacity) Reallocate(Memory::GrowCapacity(capacity));

        data[count] = std::move(copy);
        count++;
    }

    void Insert(std::size_t index, const T& value) {
        ASSERT(index <= count, "index out of range");

        T copy = value;
        if (count >= capacity) Reallocate(Memory::GrowCapacity(capacity));
        for (std::size_t i = count; i > index; i--) {
            data[i] = data[i - 1];
        }
        
        data[index] = copy;
        count++;
    }

    void Append(const T* source, std::size_t numItems) {
        if (numItems == 0) return;

        const std::size_t required = count + numItems;
        if (required > capacity) {
            std::size_t newCapacity = capacity;
            while (newCapacity < required) {
                newCapacity = Memory::GrowCapacity(newCapacity);
            }
            Reallocate(newCapacity);
        }

        std::memcpy(data + count, source, numItems * sizeof(T));
        count += numItems;
    }
    
    template<typename... Args>
    T& EmplaceBack(Args&&... args) {
        T temp(std::forward<Args>(args)...);
        if (count >= capacity) Reallocate(Memory::GrowCapacity(capacity));

        new (&data[count]) T(std::move(temp));
        return data[count++];
    } 

    void Erase(std::size_t index) {
        ASSERT(index < count, "index out of range");
        
        data[index].~T();
        for (int i = index; i < count - 1; i++) {
            data[i] = data[i + 1];
        }

        count--;
    } 

    void PopBack() {
        if (count > 0) {
            count--;

            // For each value we pop, call its destructor
            data[count].~T();
        }
    }

    void Clear() {
        for (std::size_t i = 0; i < count; i++) {
            data[i].~T();
        }

        count = 0;
    }

    void ShrinkToFit() {
        if (count == capacity) return;
        Reallocate(count);
    }

    T* TakeOwnership(std::size_t& outCapacity, std::size_t& outCount) {
        T* result = data;

        outCapacity = capacity;
        outCount = count;

        data = nullptr;
        capacity = 0;
        count = 0;

        return result;
    }

    // Getters/setters -----------------
    
    std::size_t Count() const { return count; }
    std::size_t Capacity() const { return capacity; }
    const T* Data() const { return data; }
    T* Data() { return data; }

    // Index operator ------------------

    T& operator[](std::size_t index) {
        ASSERT(index < count, "index out of range");
        return data[index];
    }

    const T& operator[](std::size_t index) const {
        ASSERT(index < count, "index out of range");
        return data[index];
    }

    // Iterator methods ------------------

    Iterator begin() {
        return Iterator(data);
    }


    Iterator end() {
        return Iterator(data + count);
    }

private:
    void Reallocate(std::size_t newCapacity) {
        data = Memory::GrowArray<T>(vm, data, capacity, newCapacity);
        capacity = newCapacity;
    }

    T* data{ nullptr };
    std::size_t count{ 0 };
    std::size_t capacity{ 0 };

    VMCore& vm;
};


} // Luggage