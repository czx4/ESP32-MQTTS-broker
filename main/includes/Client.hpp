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
    struct m_msg
    {
        myvector<uint8_t> m_payload;
        uint8_t m_qos = 0;
        uint16_t m_packet_id = 0;
    };
    std::optional<QueueHandle_t> m_msg_que;//TODO: rework

    bool retry_send(uint8_t *buffer_pointer, std::size_t length, std::array<uint8_t, 4> &resp_buff, uint8_t check_flag, uint16_t packetid);

public:
    enum state{
        uninit,
        processing_handshake,
        ready,
    };
    state client_state=state::uninit;
    esp_tls_t *m_tls;
    myvector<char> clientid;
    bool completed_tls=false;

    // struct Will
    // {
    //     bool presence_flag = false;
    //     bool retain = false;
    //     myvector<char> topic;
    //     myvector<uint8_t> msg;
    //     uint8_t qos = 0;
    //     void reset()
    //     {
    //         presence_flag = false;
    //         retain = false;
    //         topic = std::move(myvector<char>());
    //         msg = std::move(myvector<uint8_t>());
    //         qos = 0;
    //     }
    //     ~Will()
    //     {
    //         if (!presence_flag)
    //         {
    //             return;
    //         }
    //         publish_by_topic(topic.begin(), topic.size, msg.begin(), msg.size, qos, 0); // pack id can be random
    //         // if (retain)
    //         // {
    //         //     myvector<uint8_t> *retained_msg = retained_sub_msg.get(topic.begin(), topic.size);
    //         //     if (!retained_msg)
    //         //     {
    //         //         retained_sub_msg.add(topic.begin(), topic.size, msg);
    //         //     }
    //         //     else
    //         //     {
    //         //         retained_sub_msg.update(topic.begin(), topic.size, msg);
    //         //     }
    //         // }
    //     }
    // } will;

    Client();

    void clean_session();

    bool is_clean_session();

    bool change_connection(Client *old_client);

    void add_que_msg(uint8_t *buff_ptr, std::size_t len, uint8_t qos, uint16_t packet_id = 0);
    void read_que();

    uint8_t subscribe(char *topic_ptr, std::size_t len, uint8_t qos);

    uint8_t is_subscribed(char *topic_ptr, std::size_t len);

    void unsubscribe(char *topic_ptr, std::size_t len);

    void publish(uint8_t *buffer_pointer, std::size_t length, uint8_t qos, uint16_t packetid = 0);

    static void publish_by_topic(char *topic_ptr, std::size_t topic_len, uint8_t *buff_ptr, std::size_t buff_len, uint8_t qos, uint16_t packetid = 0);

    ~Client();
};