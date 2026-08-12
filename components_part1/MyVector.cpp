#pragma once

#include <cstddef>
#include <new>
#include <utility>
#include <stdexcept>

// ==================================================================
// MyVector simple version
// One thing you need to clear that: memory allocation(operator new) and object creation(placement new) are seperately 2 steps
// - capacity_ = how much initial memory has been applied(currently no object)
// - size_ = how many objects have been created and stored in the container
// So the destructor, copy, move always dealing with range of [0, size_) not [size, capacity_)
// ============================================================================================
template<typename T>
class MyVector {
private:
    T* data_;   // ponit to number of (capacity_ * sizeof(T)) memory's address 
    size_t size_;
    size_t capacity_;

public:
    // constructor
    MyVector() : data_(nullptr), size_(0), capacity_(0) {}

    // destructor, destroy object first then release memory
    ~MyVector() {
        // step1: destroy the created objects(total number is size_) one by one
        for (size_t i = 0; i < size_; i++) {
            data_[i].~T();
        }
        // step2: release this raw memory
        ::operator delete(data_);
    }

    // copy constructor: apply for a new memory, copy the object's element one by one
    MyVector(const MyVector& other) {
        // apply for the same size of raw memory equals object's
        data_ = static_cast<T*>(::operator new(other.size_ * sizeof(T)));
        capacity_ = other.size_;
        size_ = 0;

        for (size_t i = 0; i < other.size_; ++i) {
            // on the position of data_[i], use copy constructor to create a new object
            new (&data_[i]) T(other.data_[i]);
            size_++;
        }
    }

    // operator of copy assignment
    MyVector &operator=(const MyVector& other) {
        if (this == &other) {
            return *this;
        }

        // clear the old data and memory
        for (size_t i = 0; i < size_; ++i) {
            data_[i].~T();
        }
        ::operator delete(data_);

        // start copy
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

        // need to clear the moved object
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    // push_back function, expand the capacity when full, then construct a new element at the end
    void push_back(const T& value) {
        if (size_ == capacity_) {
            grow();
        }
        new (&data_[size_]) T(value);
        size_++;
    }

    // pop_back, destory the last element, size_ minus 1
    void pop_back() {
        size_--;
        data_[size_].~T();
    }

    // read the element
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
    // Expansion: Allocate a new, large block of memory, move the old elements over and the discard the old memory.
    void grow() {
        /*
         * 只要倍数大于1（是"乘法"关系而不是"加法"关系）本质上都能把总搬移次数压到O(n) —— 这是等比数列求和的性质:
         * capacity增长序列：1, 2, 4, 8, 16, ..., n
         * 每次扩容搬移的元素数：1, 2, 4, 8, ..., n/2
         * 总搬移次数 = 1+2+4+...+n/2 ≈ n (等比数列求和，公比2的情况下，总和小于2n)
         * 均摊到n次push_back上，每次均摊搬移次数是O(1)——这才是vector"均摊O(1) push_back"这个承诺的来源。
         * 至于具体选2倍还是1.5倍，是工程上的trade-off，不是数学上的唯一解
         */
        size_t new_capacity = (capacity_ == 0) ? 1 : capacity_ * 2;

        // step1: apply new, larger raw memory
        T* new_data = static_cast<T*>(::operator new(new_capacity * sizeof(T)));

        // step2: move old elements into new memory one by one
        for (size_t i = 0; i < size_; ++i) {
            new (&new_data[i]) T(data_[i]);
        }

        // step3: destroy all objects in old memory
        for (size_t i = 0; i < size_; ++i) {
            data_[i].~T();
        }

        // step4: release old raw memory
        ::operator delete(data_);

        // step5: pointer and capacity all point to new memory
        data_ = new_data;
        capacity_ = new_capacity;
    }
};