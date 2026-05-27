#pragma once
#include <cstdlib>
#include <memory>
#include "esp_log.h"
#include "myvector.hpp"
#include "myhashmap.hpp"
#include <new>

class Subscription
{
private:
    static constexpr uint8_t EMPTY_QOS = 255;
    static constexpr uint8_t SUB_ERROR_CODE = 0x80;
    static constexpr const char *TAG = "sub fault";

    uint8_t m_max_qos = EMPTY_QOS;

    Subscription *alloc() noexcept
    {
        return new (std::nothrow) Subscription;
    }

public:
    myhashmap<std::unique_ptr<Subscription>> m_child_nodes;
    Subscription(); // TODO: maybe custom size in future

    uint8_t add_sub(char *topic_ptr, std::size_t len, Subscription *cur_node, const uint8_t qos);
    uint8_t is_subed_to(char *topic_ptr, std::size_t len, Subscription *cur_node);
    void delete_sub(char *topic_ptr, std::size_t len, Subscription *cur_node);
    Subscription &operator=(Subscription &&s);
};
