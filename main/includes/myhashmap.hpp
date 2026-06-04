#pragma once
#include "myvector.hpp"
#include <atomic>
#include "esp_chip_info.h"
#include "esp_log.h"

template <typename T>
class myhashmap
{
private:
    static constexpr uint8_t REHASH_SIZE_TIMES = 4; // TODO: test the best num

    template <typename U>
    struct is_unique_ptr : std::false_type
    {
    };

    template <typename U>
    struct is_unique_ptr<std::unique_ptr<U>> : std::true_type
    {
    };

    uint32_t fnv1a_hash(const char *ptr, std::size_t len)
    {
        constexpr uint32_t PRIME = 16777619u;
        constexpr uint32_t OFFSET_BASIS = 2166136261u;

        uint32_t hash = OFFSET_BASIS;
        uint8_t max_iter = 2;
        if (len < 4)
            --max_iter;
        for (std::size_t i = 0; i < max_iter; ++i)
        {
            hash ^= (uint8_t)ptr[i];
            hash *= PRIME;
            hash ^= (uint8_t)ptr[len - (i + 1)];
            hash *= PRIME;
        }
        return hash;
    }

    struct Node
    {
        std::unique_ptr<Node> next = nullptr;
        myvector<char> key;
        T data;
        Node() {}
        Node(T &data_to_add, char *begin, char *end) : data(data_to_add), key(begin, end) {}
    };

    myvector<std::unique_ptr<Node>> map;

    Node *alloc_node() noexcept
    {
        return new (std::nothrow) Node;
    }

    void rehash(std::size_t new_size)
    {
        myvector<std::unique_ptr<Node>> tmpmap;
        tmpmap = std::move(map);
        if (!map.reserve(new_size))
        {
            map = std::move(tmpmap);
            return;
        }

        for (int id = 0; id < tmpmap.size; ++id)
        {
            Node *nd = tmpmap[id].get();
            if (!nd)
                continue;
            while (nd->next.get())
            {
                Node *last = nd;
                while (last->next.get()->next.get())
                {
                    last = last->next.get();
                }
                std::size_t newid = static_cast<std::size_t>(fnv1a_hash(last->next.get()->key.begin(), last->next.get()->key.size) % new_size);

                if (!map[newid].get())
                {
                    map[newid] = std::move(last->next);
                }
                else
                {
                    Node *mapptr = map[newid].get();
                    while (mapptr->next.get())
                        mapptr = mapptr->next.get();
                    mapptr->next = std::move(last->next);
                }
            }

            std::size_t newid = static_cast<std::size_t>(fnv1a_hash(tmpmap[id].get()->key.begin(), tmpmap[id].get()->key.size) % new_size);

            if (!map[newid].get())
            {
                map[newid] = std::move(tmpmap[id]);
            }
            else
            {
                Node *mapptr = map[newid].get();
                while (mapptr->next.get())
                    mapptr = mapptr->next.get();
                mapptr->next = std::move(tmpmap[id]);
            }
        }
    }

public:
    std::size_t count = 0;

    struct Iterator
    {
    private:
        Node *m_ptr;
        std::size_t cur_id;
        myvector<std::unique_ptr<Node>> *map;

    public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = T;
        using pointer = const T *;
        using reference = const T &;
        Iterator(Node *ptr, std::size_t id, myvector<std::unique_ptr<Node>> *map) : m_ptr(ptr), cur_id(id), map(map) {}

        reference operator*() const { return m_ptr->data; }
        pointer operator->() const { return &m_ptr->data; }

        Iterator &operator++()
        {
            ESP_LOGI("Hashmap","++iterator mapsize: %i",map->capacity);
            if (!m_ptr || !m_ptr->next.get())
            {
                ++cur_id;
                while (cur_id < map->capacity && map->operator[](cur_id).get() == nullptr)
                    ++cur_id;
                
                if (cur_id == map->capacity)
                {
                    m_ptr = nullptr;
                    return *this;
                }
                m_ptr = map->operator[](cur_id).get();
            }
            else
            {
                m_ptr = m_ptr->next.get();
            }
            return *this;
        }

        Iterator operator++(int)
        {
            Iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        bool operator!=(const Iterator &other)
        {
            return m_ptr != other.m_ptr;
        }
        bool operator==(const Iterator &other)
        {
            return m_ptr == other.m_ptr;
        }
    };
    Iterator begin()
    {
        for (std::size_t id = 0; id < map.capacity; id++)
        {
            if (map[id] != nullptr)
            {
                return Iterator(map[id].get(), id, &this->map);
            }
        }
        return end();
    }
    Iterator end() { return Iterator(nullptr, this->map.capacity, &this->map); }

    myhashmap(std::size_t reserve_capacity);

    template <typename U = T>
    std::enable_if_t<!is_unique_ptr<U>::value, U *>
    get(char *topic_ptr, std::size_t len)
    {
        std::size_t id = 0;
        U *return_data = nullptr;

        if (len > 0)
            id = static_cast<std::size_t>(fnv1a_hash(topic_ptr, len) % map.capacity);
        Node *ptr = map[id].get();

        while (ptr)
        {
            if (ptr->key.equal(topic_ptr, len))
            {
                return_data = &ptr->data;
                break;
            }
            ptr = ptr->next.get();
        }
        return return_data;
    }

    template <typename U = T>
    std::enable_if_t<is_unique_ptr<U>::value, typename U::element_type *>
    get(char *topic_ptr, std::size_t len)
    {
        std::size_t id = 0;
        typename U::element_type *return_data = nullptr;

        if (len > 0)
        {
            id = static_cast<std::size_t>(fnv1a_hash(topic_ptr, len) % map.capacity);
        }
        Node *ptr = map[id].get();

        while (ptr)
        {
            if (ptr->key.equal(topic_ptr, len))
            {
                return_data = ptr->data.get();
                break;
            }
            ptr = ptr->next.get();
        }
        return return_data;
    }

    bool add(char *topic_ptr, std::size_t len, T &&data);
    bool update(char *topic_ptr, std::size_t len, T &&data);
    void erase(char *topic_ptr, std::size_t len);
    myhashmap<T> &operator=(myhashmap<T> &&h);
    ~myhashmap();
};

template <typename T>
myhashmap<T>::~myhashmap() {

};

template <typename T>
myhashmap<T>::myhashmap(std::size_t reserve_capacity)
{
    map.reserve(reserve_capacity);
}

template <typename T>
bool myhashmap<T>::add(char *topic_ptr, std::size_t len, T &&data)
{
    std::size_t id = 0;
    if (len > 0)
        id = static_cast<std::size_t>(fnv1a_hash(topic_ptr, len) % map.capacity);

    Node *new_node_ptr = alloc_node();
    if (!new_node_ptr)
    {
        return false;
    }

    if constexpr (is_unique_ptr<T>::value)
    {
        new_node_ptr->data = std::move(data);
    }
    else
    {
        new_node_ptr->data = data;
    }
    myvector<char> key_helper(topic_ptr, topic_ptr + len);
    new_node_ptr->key = std::move(key_helper);
    if (new_node_ptr->key.size != len)
    {
        delete (new_node_ptr);
        return false;
    }

    Node *ptr = map[id].get();
    if (!ptr)
    {
        ++count;
        map[id].reset(new_node_ptr);
    }
    else
    {
        while (ptr->next.get())
            ptr = ptr->next.get();

        ptr->next.reset(new_node_ptr);
        if (++count > map.capacity * REHASH_SIZE_TIMES)
        {
            rehash(count / 2); // TODO: test the best num and the cost of rehash
        }
    }
    return true;
}

template <typename T>
bool myhashmap<T>::update(char *topic_ptr, std::size_t len, T &&data)
{
    std::size_t id = 0;
    if (len > 0)
        id = static_cast<std::size_t>(fnv1a_hash(topic_ptr, len) % map.capacity);

    Node *ptr = map[id].get();
    while (ptr && !ptr->key.equal(topic_ptr, len))
    {
        ptr = ptr->next.get();
    }
    if (!ptr)
    {
        return false;
    }
    if constexpr (is_unique_ptr<T>::value)
    {
        ptr->data = std::move(data);
    }
    else
    {
        ptr->data = data;
    }
    return true;
}

template <typename T>
void myhashmap<T>::erase(char *topic_ptr, std::size_t len)
{
    std::size_t id = 0;
    if (len > 0)
        id = static_cast<std::size_t>(fnv1a_hash(topic_ptr, len) % map.capacity);
    Node *ptr = map[id].get();
    if (ptr->key.equal(topic_ptr, len))
    {
        map[id] = std::move(ptr->next);
        --count;
        return;
    }
    while (ptr->next.get() && !ptr->next.get()->key.equal(topic_ptr, len))
    {
        ptr = ptr->next.get();
    }
    if (ptr->next.get())
    {
        ptr->next = std::move(ptr->next->next);
        --count;
    }
}

template <typename T>
myhashmap<T> &myhashmap<T>::operator=(myhashmap<T> &&h)
{
    count = h.count;
    map = std::move(h.map);

    return *this;
}