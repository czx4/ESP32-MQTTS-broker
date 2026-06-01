#pragma once
#include "myvector.hpp"

class Publishhashmap // I expect small size so degradation in worst case (probing) to O(n) is acceptible
{
private:
    std::size_t power_of_cap;
    uint32_t knuth_hash(uint16_t x)
    {
        constexpr uint32_t knuth = 2654435769;
        const uint32_t y = x;
        return (y * knuth) >> (32 - power_of_cap);
    }
    bool rehash()
    {
        myvector<Node_pubmap> new_map;
        if (!new_map.reserve(map.capacity * 2))
            return false;
        ++power_of_cap;
        for (Node_pubmap &node : map)
        {
            if (node.count == 0)
                continue;
            std::size_t id = knuth_hash(node.pubpack_id);
            if (new_map[id].count != 0)
            {
                std::size_t cur_id = id + 1;
                for (; cur_id != id; ++id)
                {
                    if (cur_id == new_map.capacity)
                    {
                        cur_id = 0;
                    }
                    if (new_map[cur_id].count == 0)
                        break;
                }
                id = cur_id;
            }
            new_map[id].count = node.count;
            new_map[id].pubpack_id = node.pubpack_id;
            new_map[id].qos = node.qos;
            new_map[id].data = std::move(node.data);
        }
        new_map.size = map.size;
        map = std::move(new_map);
        return true;
    }

public:
    struct Node_pubmap
    {
        uint8_t count = 0;
        uint8_t qos = 0;
        uint16_t pubpack_id = 0;
        myvector<uint8_t> data;
    };
    myvector<Node_pubmap> map;
    Publishhashmap(std::size_t init_cap);
    Node_pubmap *get(uint16_t pubpack_get_id);
    bool add(uint16_t pubpack_id, uint8_t *data_ptr, std::size_t data_len, uint8_t msg_qos, uint8_t init_count = 1);
    void erase(uint16_t pubpack_id);
    ~Publishhashmap();
};
