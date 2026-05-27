#pragma once
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <algorithm>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

template <typename T>
class myvector
{
private:
    static constexpr const size_t CAPACITY_RESIZE = 2;
    std::unique_ptr<T[]> alloc_v(std::size_t n) noexcept
    {
        return std::unique_ptr<T[]>(new (std::nothrow) T[n]);
    }
    std::unique_ptr<T[]> data = nullptr;

public:
    std::size_t capacity = 0;
    std::size_t size = 0;
    myvector();
    myvector(T *beg, T *end);
    myvector(std::initializer_list<T> list);
    myvector(T val, std::size_t count);
    myvector(myvector<T> &vec);
    ~myvector() {}
    myvector &operator=(myvector<T> &vec);
    myvector &operator=(myvector<T> &&vec);
    bool operator==(const myvector<T> &vec);
    T &operator[](std::size_t id);
    T *begin();
    T *end();
    void push_back(T val);
    void shrink_to_fit();
    bool reserve(std::size_t cap);
    void fill(T val);
    bool equal(T *ptr, std::size_t len);
    bool insert(std::size_t pos, T data);
};

#include "../myvector.ipp"