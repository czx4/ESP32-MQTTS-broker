#pragma once
#include "main.hpp"
#include "Subscription.hpp"
#include "myvector.hpp"

class Client
{
    static constexpr uint8_t MAX_QUED_MSGS = 5; // TODO also maybe some enums, add some common constants to main.hpp
    static constexpr uint16_t MS_TO_WAIT = 1000;
    static constexpr uint8_t PUBREL = 0x62;
    static constexpr uint8_t PUBREC = 0x50;
    static constexpr uint8_t PUBCOMP = 0x70;
    static constexpr uint8_t PUBACK = 0x40;
    static constexpr uint8_t SUB_ERROR_CODE = 0x80;
    static constexpr uint8_t EMPTY_QOS = 255;
    static constexpr const char *TAG = "client fault";

private:
    Subscription m_subscriptions = Subscription();
    bool retry_send(uint8_t *buffer_pointer, std::size_t length, std::array<uint8_t, 4> &resp_buff, uint8_t check_flag, uint16_t packetid);

public:
    bool has_qued_msgs = false;
    int sock = -1;
    bool clean_session = true;

    struct Will
    {
        bool presence_flag = false;
        bool retain = false;
        myvector<char> topic;
        myvector<uint8_t> msg;
        uint8_t qos = 0;
    } will;

    Client();

    void del_subs();

    uint8_t subscribe(char *topic_ptr, std::size_t len, uint8_t qos);

    uint8_t is_subscribed(char *topic_ptr, std::size_t len);

    void unsubscribe(char *topic_ptr, std::size_t len);

    ~Client();
};