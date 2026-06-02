#include "Subscription.hpp"

Subscription::Subscription() : m_child_nodes(1) {};

uint8_t Subscription::add_sub(char *topic_ptr, std::size_t len, Subscription *cur_node, const uint8_t qos)
{ // TODO: maybe add max subtopic nesting to counter malicious topic addition
    while (len > 0)
    {
        char *end = topic_ptr;
        while (end < topic_ptr + len && *end!='/')
            ++end;
        len -= (end - topic_ptr);
        Subscription * tmp = cur_node->m_child_nodes.get(topic_ptr,end - topic_ptr);
        if(!tmp)
        {
            tmp=alloc();
            if(!tmp)
                return SUB_ERROR_CODE;
            cur_node->m_child_nodes.add(topic_ptr,end-topic_ptr,std::unique_ptr<Subscription>(tmp));
            tmp = cur_node->m_child_nodes.get(topic_ptr, end - topic_ptr);
            if(!tmp)
                return SUB_ERROR_CODE;
        }
        cur_node = tmp;
        topic_ptr=end;
    }
    cur_node->m_max_qos = qos;
    return qos;
}

uint8_t Subscription::is_subed_to(char *topic_ptr, std::size_t len, Subscription *cur_node)
{
    while (len > 0)
    {
        char *end = topic_ptr;
        while (end < topic_ptr + len && *end != '/')
            ++end;
        len -= (end - topic_ptr);
        cur_node = cur_node->m_child_nodes.get(topic_ptr, end - topic_ptr);
        if (!cur_node)
            return EMPTY_QOS;
        topic_ptr=end;
    }
    return cur_node->m_max_qos;
}

void Subscription::delete_sub(char *topic_ptr, std::size_t len, Subscription *cur_node)
{
    Subscription *del_parent = cur_node;
    char *del_topic_ptr = nullptr;
    std::size_t del_topic_len = 0;
    while (true)
    { // TODO: maybe add some safe guard
        char *end = topic_ptr;
        while (end < topic_ptr + len && *end != '/')
            ++end;

        std::size_t subtopic_len = end - topic_ptr;
        if (len == subtopic_len)
        {
            Subscription *gnode = cur_node->m_child_nodes.get(topic_ptr, subtopic_len);
            if (gnode->m_child_nodes.count > 0)
            {
                gnode->m_max_qos = EMPTY_QOS;
                return;
            }
            else if (del_topic_ptr)
            {
                del_parent->m_child_nodes.erase(del_topic_ptr, del_topic_len);
                return;
            }
            else
            {
                cur_node->m_child_nodes.erase(topic_ptr, subtopic_len);
                return;
            }
        }
        len -= subtopic_len;

        Subscription *del_p_node = cur_node->m_child_nodes.get(topic_ptr, subtopic_len);
        if (!del_p_node)
            return;
        if (del_p_node->m_max_qos != EMPTY_QOS || del_p_node->m_child_nodes.count > 1)
        {
            del_parent = del_p_node;
            del_topic_ptr = nullptr;
            del_topic_len = 0;
        }
        else if (!del_topic_ptr)
        {
            del_topic_ptr = topic_ptr;
            del_topic_len = subtopic_len;
        }
        topic_ptr = end;
    }
}

Subscription &Subscription::operator=(Subscription &&s) noexcept
{
    m_max_qos = s.m_max_qos;
    m_child_nodes = std::move(s.m_child_nodes);
    return *this;
}