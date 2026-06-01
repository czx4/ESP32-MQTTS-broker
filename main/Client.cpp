#include "Client.hpp"

Client::Client() {}

void Client::del_subs()
{
    m_subscriptions = Subscription();
}

uint8_t Client::subscribe(char *topic_ptr, std::size_t len, uint8_t qos)
{
    uint8_t ret_code = m_subscriptions.add_sub(topic_ptr, len, &m_subscriptions, qos);
    return ret_code;
}

uint8_t Client::is_subscribed(char *topic_ptr, std::size_t len)
{
    uint8_t code = m_subscriptions.is_subed_to(topic_ptr, len, &m_subscriptions);
    return code;
}

void Client::unsubscribe(char *topic_ptr, std::size_t len)
{
    m_subscriptions.delete_sub(topic_ptr, len, &m_subscriptions);
}

Client::~Client()
{
}