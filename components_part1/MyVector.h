#pragma once

#include <cstddef>
#include <new>
#include <utility>
#include <stdexcept>

// ==================================================================
// MyVector simple version
// One thing you need to make clear: memory allocation (operator new) and
// object construction (placement new) are two separate steps.
// - capacity_ = how much raw memory has been allocated (currently no objects there)
// - size_     = how many objects have actually been constructed and stored
// So the destructor, copy, and move logic always deal with the range
// [0, size_), never [size_, capacity_).
// ==================================================================
template<typename T>
class MyVector {
private:
    T* data_;   // points to a block of (capacity_ * sizeof(T)) bytes of memory
    size_t size_;
    size_t capacity_;

public:
    // constructor
    MyVector() : data_(nullptr), size_(0), capacity_(0) {}

    // destructor: destroy objects first, then release the memory
    ~MyVector() {
        // step1: destroy the constructed objects (total count is size_) one by one
        for (size_t i = 0; i < size_; i++) {
            data_[i].~T();
        }
        // step2: release the raw memory
        ::operator delete(data_);
    }

    // copy constructor: allocate new memory, copy-construct each element one by one
    MyVector(const MyVector& other) {
        // allocate raw memory sized for other's elements
        data_ = static_cast<T*>(::operator new(other.size_ * sizeof(T)));
        capacity_ = other.size_;
        size_ = 0;

        for (size_t i = 0; i < other.size_; ++i) {
            // at position data_[i], use the copy constructor to build a new object
            new (&data_[i]) T(other.data_[i]);
            size_++;
        }
    }

    // copy assignment operator
    MyVector &operator=(const MyVector& other) {
        if (this == &other) {
            return *this;
        }

        // clear the old data and memory
        for (size_t i = 0; i < size_; ++i) {
            data_[i].~T();
        }
        ::operator delete(data_);

        // start copying
        data_ = static_cast<T*>(::operator new(other.size_ * sizeof(T)));
        capacity_ = other.size_;
        size_ = 0;
        for (size_t i = 0; i < other.size_; ++i) {
            new (&data_[i]) T(other.data_[i]);
            size_++;
        }

        return *this;
    }

    // move constructor
    MyVector(MyVector &&other) noexcept {
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;

        // the moved-from object must be cleared out
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    // push_back: grow the capacity when full, then construct a new element at the end
    void push_back(const T& value) {
        if (size_ == capacity_) {
            grow();
        }
        new (&data_[size_]) T(value);
        size_++;
    }

    // pop_back: destroy the last element, decrement size_
    void pop_back() {
        size_--;
        data_[size_].~T();
    }

    // read/write access to an element
    T& operator[](size_t idx) {
        return data_[idx];
    }

    T& at(size_t idx) {
        if (idx >= size_) {
            throw std::out_of_range("index out of range");
        }
        return data_[idx];
    }

    // query information
    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }
    bool empty() const { return size_ == 0; }

private:
    // Expansion: allocate a new, larger block of memory, move the old elements
    // over, then discard the old memory.
    void grow() {
        /*
         * As long as the growth factor is greater than 1 (a multiplicative
         * relationship rather than an additive one), the total number of
         * element moves is bounded by O(n) — this follows from the geometric
         * series sum:
         *   capacity sequence:        1, 2, 4, 8, 16, ..., n
         *   elements moved per grow:  1, 2, 4, 8, ..., n/2
         *   total moves = 1+2+4+...+n/2 ≈ n (geometric sum with ratio 2 is < 2n)
         * Amortized over n push_back calls, each one does O(1) work on
         * average — this is where vector's "amortized O(1) push_back"
         * guarantee comes from.
         * Whether the factor is exactly 2, 1.5, or something else is an
         * engineering trade-off, not a mathematical requirement.
         */
        size_t new_capacity = (capacity_ == 0) ? 1 : capacity_ * 2;

        // step1: allocate new, larger raw memory
        T* new_data = static_cast<T*>(::operator new(new_capacity * sizeof(T)));

        // step2: move the old elements into the new memory one by one.
        // std::move(data_[i]) casts the element to an rvalue reference, so
        // T's move constructor is selected instead of the copy constructor
        // (assuming T actually provides one — otherwise this silently falls
        // back to copying, which is still correct, just not faster).
        for (size_t i = 0; i < size_; ++i) {
            new (&new_data[i]) T(std::move(data_[i]));
        }

        // step3: destroy all objects in the old memory
        // (moved-from objects are still valid objects and must be destroyed,
        // even though their internal resources have already been "stolen")
        for (size_t i = 0; i < size_; ++i) {
            data_[i].~T();
        }

        // step4: release the old raw memory
        ::operator delete(data_);

        // step5: point data_ and capacity_ at the new memory
        data_ = new_data;
        capacity_ = new_capacity;
    }
};