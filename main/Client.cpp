#include "Client.hpp"

Client::Client(){}

bool Client::retry_send(uint8_t *buffer_pointer, std::size_t length, std::array<uint8_t, 4> &resp_buff, uint8_t check_flag, uint16_t packetid)
{
    uint8_t num_tries = 0;
    uint16_t pubid_check = (resp_buff[2] << 8) | resp_buff[3];
    while (resp_buff[0] != check_flag || resp_buff[1] != 2 || pubid_check != packetid)
    {
        if (++num_tries > 5)
            return false;
        esp_tls_conn_write(m_tls, buffer_pointer, length); // dependent on semaphore in Client::publish
        esp_tls_conn_read(m_tls, resp_buff.begin(), resp_buff.size());
        pubid_check = (resp_buff[2] << 8) | resp_buff[3];
    }
    return true;
}

void Client::clean_session()
{
    m_msg_que.emplace(xQueueCreate(MAX_QUED_MSGS, sizeof(m_msg)));
}

bool Client::is_clean_session()
{
    return !(m_msg_que.has_value());
}

bool Client::change_connection(Client *old_client)
{ // TODO: send disconnect before dropping connection
    m_subscriptions = std::move(old_client->m_subscriptions);
    if (m_subscriptions.m_child_nodes.count != old_client->m_subscriptions.m_child_nodes.count)
    {
        return false;
    }
    return true;
}

void Client::add_que_msg(uint8_t *buff_ptr, std::size_t len, uint8_t qos, uint16_t packet_id)
{
    if (!m_msg_que.has_value())
    {
        ESP_LOGW(TAG, "queue doesnt exist");
        return;
    }

    m_msg msg_to_que;
    myvector<uint8_t> tmp(buff_ptr, buff_ptr + len);
    if (tmp.size != len)
        return;
    msg_to_que.m_payload = tmp;
    msg_to_que.m_qos = qos;
    msg_to_que.m_packet_id = packet_id;

    if (xQueueSendToBack(m_msg_que.value(), &msg_to_que, MS_TO_WAIT) != pdPASS)
    {
        ESP_LOGW(TAG, "queue error");
    }
}

void Client::read_que()
{
    if (!m_msg_que.has_value())
    {
        ESP_LOGW(TAG, "que doesnt exist");
        return;
    }

    while (uxQueueMessagesWaiting(m_msg_que.value()) > 0){
        m_msg que_first;
        if (xQueueReceive(m_msg_que.value(), &que_first, MS_TO_WAIT) != pdPASS)
        {
            ESP_LOGW(TAG, "error while reading que");
            return;
        }
        publish(que_first.m_payload.begin(), que_first.m_payload.size, que_first.m_qos, que_first.m_packet_id);
    }
}

// uint8_t Client::subscribe(char *topic_ptr, std::size_t len, uint8_t qos)
// {
//     uint8_t ret_code = m_subscriptions.add_sub(topic_ptr, len, &m_subscriptions, qos);

//     myvector<uint8_t> *retained_msg = retained_sub_msg.get(topic_ptr, len);
//     if (retained_msg != nullptr)
//     {
//         publish(retained_msg->begin(), retained_msg->size, qos, 1);
//     }
//     return ret_code;
// }

uint8_t Client::is_subscribed(char *topic_ptr, std::size_t len)
{
    uint8_t code = m_subscriptions.is_subed_to(topic_ptr, len, &m_subscriptions);
    return code;
}

void Client::unsubscribe(char *topic_ptr, std::size_t len)
{
    m_subscriptions.delete_sub(topic_ptr, len, &m_subscriptions);
}

// void Client::publish(uint8_t *buffer_pointer, size_t length, uint8_t qos, uint16_t packetid)
// {
//     // if(eTaskGetState(m_task_handle)==eDeleted && m_msg_que.has_value()){ //TODO: commented out for a moment
//     //     add_que_msg(buffer_pointer,length,qos,packetid);
//     //     return;
//     // }
//     esp_tls_conn_write(m_tls, buffer_pointer, length);
//     if (qos == 0 || packetid == 0)
//     {
//         return;
//     }
//     std::array<uint8_t, 4> resp_buff;
//     struct timeval timeout;
//     timeout.tv_sec = 2;
//     timeout.tv_usec = 0;
//     setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
//     esp_tls_conn_read(m_tls, resp_buff.begin(), resp_buff.size());
//     uint16_t pubid_check = (resp_buff[2] << 8) | resp_buff[3];

//     if (qos == 1)
//     {
//         if (resp_buff[0] != PUBACK || resp_buff[1] != 2 || pubid_check != packetid)
//         {
//             *buffer_pointer |= 0x08;
//             if (!retry_send(buffer_pointer, length, resp_buff, PUBACK, packetid))
//             {
//                 ESP_LOGW(TAG, "failed to resend msg");
//             }
//         }
//     }
//     else if (qos == 2)
//     {
//         if (resp_buff[0] != PUBREC || resp_buff[1] != 2 || pubid_check != packetid)
//         {
//             if (!retry_send(buffer_pointer, length, resp_buff, PUBREC, packetid))
//             {
//                 ESP_LOGW(TAG, "failed to get pubrec");
//                 timeout.tv_sec = 0;
//                 setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
//                 return;
//             }
//         }
//         resp_buff[0] = 0x62;
//         std::array<uint8_t, 4> pubcomp;

//         esp_tls_conn_write(m_tls, resp_buff.begin(), resp_buff.size());
//         esp_tls_conn_read(m_tls, pubcomp.begin(), pubcomp.size());

//         pubid_check = (pubcomp[2] << 8) | pubcomp[3];
//         if (pubcomp[0] != PUBCOMP || pubcomp[1] != 2 || pubid_check != packetid)
//         {
//             if (!retry_send(resp_buff.begin(), resp_buff.size(), pubcomp, PUBCOMP, packetid))
//             {
//                 ESP_LOGW(TAG, "failed to get pubrec");
//             }
//         }
//     }
//     timeout.tv_sec = 0;
//     setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
// }

// void Client::publish_by_topic(char *topic_ptr, std::size_t topic_len, uint8_t *buff_ptr, std::size_t buff_len, uint8_t qos, uint16_t packetid)
// {
//     for (auto client = cid_client.begin(); client != cid_client.end(); ++client)
//     { // for each instead of 2nd hash map to save memory
//         if (client->get()->is_subscribed(topic_ptr, topic_len))
//         {
//             client->get()->publish(buff_ptr, buff_len, qos, packetid);
//         }
//     }
// }

// Client::~Client()
// {
//     esp_tls_server_session_delete(m_tls);
//     shutdown(m_socket, SHUT_RDWR);
//     closesocket(m_socket);
// }