#include "Publishhashmap.hpp"

Publishhashmap::Publishhashmap(std::size_t init_cap)
{
    assert(init_cap && !(init_cap & (init_cap - 1)) && "cap must be a power of 2");
    power_of_cap = __builtin_ctz(init_cap);
    map.reserve(init_cap);
}

Publishhashmap::Node_pubmap *Publishhashmap::get(uint16_t pubpack_get_id)
{
    std::size_t id = knuth_hash(pubpack_get_id);
    if (map[id].pubpack_id != pubpack_get_id)
    {
        std::size_t cur_id = id + 1;
        for (; cur_id != id; ++id)
        {
            if (cur_id == map.capacity)
            {
                cur_id = 0;
            }
            if (map[cur_id].pubpack_id == pubpack_get_id)
            {
                break;
            }
        }
        if (cur_id == id)
            return nullptr;
        id = cur_id;
    }
    return &map[id];
}

bool Publishhashmap::add(uint16_t pubpack_id, uint8_t *data_ptr, std::size_t data_len, uint8_t msg_qos, uint8_t init_count)
{
    if (map.size == map.capacity)
    {
        if (!rehash())
            return false;
    }
    std::size_t id = knuth_hash(pubpack_id);
    if (map[id].count != 0)
    {
        std::size_t cur_id = id + 1;
        for (; cur_id != id; ++id)
        {
            if (cur_id == map.capacity)
            {
                cur_id = 0;
            }
            if (map[cur_id].count == 0)
                break;
        }
        id = cur_id;
    }
    map[id].data = myvector<uint8_t>(data_ptr, data_ptr + data_len);
    if (!map[id].data.begin())
        return false;
    map[id].pubpack_id = pubpack_id;
    map[id].count = init_count;
    map[id].qos = msg_qos;
    ++map.size;
    return true;
}

void Publishhashmap::erase(uint16_t pubpack_id)
{
    Publishhashmap::Node_pubmap *node = this->get(pubpack_id);
    if (!node)
        return;
    node->count = 0;
    node->pubpack_id = 0;
    node->qos = 0;
    node->data = std::move(myvector<uint8_t>());
    --map.size;
}

Publishhashmap::~Publishhashmap()
{
}