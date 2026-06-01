#include "worker.hpp"

constexpr std::size_t BUFF_SIZE = 1024;
constexpr std::size_t STARTING_CLIENT_STORE_SIZE = 10;
constexpr std::size_t INITIAL_PUBLISH_MSG_STORE_SIZE = 32;
static constexpr const char *TAG = "worker";

myhashmap<std::unique_ptr<Client>> client_store(STARTING_CLIENT_STORE_SIZE);
Publishhashmap publish_msg_store(INITIAL_PUBLISH_MSG_STORE_SIZE);
myhashmap<myvector<uint8_t>> retained_sub_msg(1);

user_creds user_cred(1);

static bool sendtoque(message &msg)
{
    if (xQueueSendToBack(socks_for_fds, &msg, portMAX_DELAY) != pdPASS) // TODO set some kind of timeout
    {
        ESP_LOGI(TAG, "failed to send to socks_for_fds");
        return false;
    }
    return true;
}

void add_packet_id(sock_cid *sockst, sock_cid::packet_state state_to_add, uint16_t packet_id)
{
    bool added = false; // TODO: maybe add ignoring the duplicate packet_ids
    for (auto &state_pair : sockst->packid_state)
    {
        if (state_pair.first == 0)
        {
            state_pair.first = packet_id;
            state_pair.second = state_to_add;
            added = true;
            break;
        }
    }
    if (!added)
    {
        sockst->packid_state.push_back({packet_id, state_to_add}); // TODO: can fail due to memory
    }
}

static void drop_conn(message &msg, sock_cid *sockst, std::array<sock_cid, 64> &sock_to_clientid, bool sendwill = true)
{
    msg.op = message::operation::delall;
    xQueueSendToBack(socks_for_fds, &msg, portMAX_DELAY); // TODO set some timeout
    esp_tls_server_session_delete(sockst->tls);
    closesocket(msg.socket);
    if (sockst->client && sockst->client->will.presence_flag)
    {
        for (auto &client : client_store)
        {
            if (client->sock == sockst->client->sock)
                continue;
            if (client->sock != -1 && client->is_subscribed(sockst->client->will.topic.begin(), sockst->client->will.topic.size))
            {
                message msg(client->sock, message::operation::write);
                if (sendtoque(msg))
                {
                    add_packet_id(&sock_to_clientid[client->sock], sock_cid::packet_state::sendpublish, 'w');
                }
            }
        }
    }
    if (sockst->client && sockst->client->clean_session)
    {
        client_store.erase(sockst->clientid.begin(), sockst->clientid_len);
    }
    else if (sockst->client)
    {
        sockst->client->sock = -1;
    }
    sockst->phs = sock_cid::phase::uninit;
    sockst->client = nullptr;
    sockst->clientid_len = 0;

    ESP_LOGI(TAG, "connection failed");
}

static void conn_ack(esp_tls_t *tls, uint8_t return_code, bool session_present_flag)
{
    // If the Server accepts a connection with CleanSession set to 0, the value set in Session Present depends on whether the Server already has stored Session state for the supplied client ID
    std::array<uint8_t, 4> connack;
    connack[0] = 0x20;
    connack[1] = 0x02;
    connack[2] = !session_present_flag;
    connack[3] = return_code;
    esp_tls_conn_write(tls, connack.begin(), connack.size());
    ESP_LOGI(TAG, "sent connack");
}

static mqtt_conn_return connect_mqtt(std::array<uint8_t, BUFF_SIZE> &buf, uint8_t * cur_pos, sock_cid *sockst, Client::Will &will);

void publish(uint8_t *buff_ptr, std::size_t len, uint8_t qos, uint16_t packet_id, sock_cid *sockst)
{
    if (sockst->client->sock == -1)
    {
        if (!sockst->client->clean_session)
        {
            Publishhashmap::Node_pubmap *node = publish_msg_store.get(packet_id);
            if (!node)
            {
                *buff_ptr |= DUPLICATION_FLAG;
                publish_msg_store.add(packet_id, buff_ptr, len, qos);
            }
            else
            {
                node->count++;
            }
            sockst->client->qued_msg_pack_id = packet_id;
        }
        return;
    }
    int ret_write = esp_tls_conn_write(sockst->tls, buff_ptr, len);
    ESP_LOGI(TAG,"published with %i return code msg was: %i sent to socket: %i",ret_write,*buff_ptr,sockst->client->sock);
    if (qos == 0)
    {
        return;
    }
    sock_cid::packet_state state_to_add = qos == 1 ? sock_cid::packet_state::getpuback : sock_cid::packet_state::getpubrec;
    add_packet_id(sockst, state_to_add, packet_id);
    Publishhashmap::Node_pubmap *node = publish_msg_store.get(packet_id);
    if (!node)
    {
        *buff_ptr |= DUPLICATION_FLAG;
        publish_msg_store.add(packet_id, buff_ptr, len, qos);
    }
    else
    {
        node->count++;
    }
}

void publish_by_topic(char *topic_ptr, std::size_t topic_len, uint8_t *buff_ptr, std::size_t len, uint8_t qos, uint16_t packet_id, std::array<sock_cid, 64> &sock_to_clientid)
{
    if(packet_id==0){
        packet_id=5;//TODO: in future random
    }
    if (!publish_msg_store.add(packet_id, buff_ptr, len, qos))
        return;
    ESP_LOGI(TAG,"got to pub_by_topic len of packet: %i",len);
    for (auto &client : client_store)
    {
        if (client->sock != -1 && client->is_subscribed(topic_ptr, topic_len))
        {
            ESP_LOGI(TAG,"found clinet to pub to");
            message msg(client->sock, message::operation::delread);
            sendtoque(msg);
            msg.op=message::operation::write;
            if (sendtoque(msg))
            {
                add_packet_id(&sock_to_clientid[client->sock], sock_cid::packet_state::sendpublish, packet_id);
            }else{
                msg.op=message::operation::read;
                sendtoque(msg);
            }
        }
    }
}

void worker(void *args)
{
    std::array<sock_cid, 64> sock_to_clientid;
    // sock_cid sock_to_clientid[64];
    message msg(-1, message::operation::read);
    std::array<uint8_t, BUFF_SIZE> buf;

    esp_tls_cfg_server tls_cfg{};
    tls_cfg.cacert_buf = NULL;
    tls_cfg.cacert_bytes = 0;
    tls_cfg.servercert_buf = pem_cert;
    tls_cfg.servercert_bytes = sizeof(pem_cert);
    tls_cfg.serverkey_buf = pem_prv_key;
    tls_cfg.serverkey_bytes = sizeof(pem_prv_key);

    int recieved_len;
    // uint8_t packet_type = 0;
    // uint8_t packet_flags = 0;
    uint8_t *cur_pos = nullptr;

    uint16_t packet_length = 0;
    uint16_t multiplier = 1;
    uint8_t enc_byte = 0;

    // uint8_t conn_flags = 0;
    // uint16_t clientid_len = 0;

    // bool username = false;
    // bool password = false;
    while (true)
    {
        while (xQueueReceive(pending_socks, &msg, portMAX_DELAY) == pdPASS)
        {
            ESP_LOGI(TAG, "got to worker %i", msg.socket);
            if (msg.socket == -1)
                continue;
            int sock = msg.socket;

            sock_cid *sockst = &sock_to_clientid[sock];
            int code = -1;
            if (sockst->phs == sock_cid::phase::uninit)
            {
                sockst->tls = esp_tls_init();
                if (!sockst->tls)
                {
                    ESP_LOGE(TAG, "TLS init allocation failed");
                    closesocket(msg.socket);
                    continue;
                }
                esp_tls_server_session_init(&tls_cfg, sock, sockst->tls);
                code = esp_tls_server_session_continue_async(sockst->tls);
                if (code < 0)
                {
                    drop_conn(msg, sockst, sock_to_clientid);
                    continue;
                }
                else if (code == 0)
                {
                    sockst->phs = sock_cid::phase::mqtt;
                    msg.op = message::operation::read;
                    sendtoque(msg);
                    continue;
                }
                else if (code == ESP_TLS_ERR_SSL_WANT_WRITE)
                {
                    msg.op = message::operation::write;
                    sendtoque(msg);
                    sockst->phs = sock_cid::phase::tls_handshake;
                    continue;
                }
                else if (code == ESP_TLS_ERR_SSL_WANT_READ)
                {
                    msg.op = message::operation::read;
                    sendtoque(msg);
                    sockst->phs = sock_cid::phase::tls_handshake;
                    continue;
                }
            }
            else if (sockst->phs == sock_cid::phase::tls_handshake)
            {
                code = esp_tls_server_session_continue_async(sockst->tls);
                if (code < 0)
                {
                    drop_conn(msg, sockst, sock_to_clientid);
                    continue;
                }
                else if (code == 0)
                {
                    msg.op = message::operation::read;
                    sockst->phs = sock_cid::phase::mqtt;
                    sendtoque(msg);
                    continue;
                }
                else if (code == ESP_TLS_ERR_SSL_WANT_WRITE)
                {
                    msg.op = message::operation::write;
                    sendtoque(msg);
                    continue;
                }
                else if (code == ESP_TLS_ERR_SSL_WANT_READ)
                {
                    msg.op = message::operation::read;
                    sendtoque(msg);
                    continue;
                }
            }

            // MQTT PHASE
            if (msg.op == message::operation::read)
            {
                recieved_len = esp_tls_conn_read(sockst->tls, buf.begin(), 1);
                if (recieved_len <= 0)
                {
                    ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
                    drop_conn(msg, sockst, sock_to_clientid);
                    continue;
                }
                cur_pos=&buf[1];
                packet_length = 0;
                multiplier = 1;
                bool len_error=false;
                uint8_t additional_packet_len=1;
                do
                {
                    if(esp_tls_conn_read(sockst->tls,cur_pos,1)<=0){
                        len_error=true;
                        break;
                    }
                    enc_byte = *cur_pos++;
                    packet_length += (enc_byte & 127) * multiplier;
                    multiplier *= 128;
                    if (multiplier > 128 * 128 * 128)
                    {
                        ESP_LOGW(TAG, "msg len malformed");
                        len_error=true;
                        break;
                    }
                    ++additional_packet_len;
                } while ((enc_byte & 128) != 0);

                if(len_error || packet_length+additional_packet_len>1024){
                    ESP_LOGI(TAG,"length error");
                    drop_conn(msg,sockst,sock_to_clientid);
                    continue;
                }

                {                    
                std::size_t sum=0;
                uint8_t* tmp_ptr=cur_pos;

                while(sum<packet_length){
                    int read_code=esp_tls_conn_read(sockst->tls,tmp_ptr,packet_length-sum);
                    if(read_code<=0){
                        len_error=true;
                        break;
                    }
                    sum+=read_code;
                    tmp_ptr+=read_code;
                }
                }

                if(len_error){
                    ESP_LOGI(TAG,"length error");
                    drop_conn(msg,sockst,sock_to_clientid);
                    continue;
                }

                packet_length+=additional_packet_len;

                if (sockst->clientid_len == 0)
                {
                    Client::Will will;
                    mqtt_conn_return ret = connect_mqtt(buf, cur_pos, sockst, will);
                    if (ret == mqtt_conn_return::disconnect)
                    {
                        drop_conn(msg, sockst, sock_to_clientid);
                        continue;
                    }
                    sockst->client = client_store.get(sockst->clientid.begin(), sockst->clientid_len);
                    if (sockst->client == nullptr)
                    {
                        Client *new_client = new (std::nothrow) Client;
                        if (!new_client || !client_store.add(sockst->clientid.begin(), sockst->clientid_len, std::unique_ptr<Client>(new_client)))
                        {
                            drop_conn(msg, sockst, sock_to_clientid);
                            continue;
                        }
                        sockst->client = client_store.get(sockst->clientid.begin(), sockst->clientid_len);
                        sockst->client->sock = sock;
                    }
                    else if (sockst->client->sock != -1)
                    {
                        message prev_client_msg(sockst->client->sock, message::operation::delall);
                        sendtoque(prev_client_msg);

                        sock_cid *previously_connected_client = &sock_to_clientid[sockst->client->sock];
                        previously_connected_client->phs = sock_cid::phase::uninit;
                        previously_connected_client->client = nullptr;
                        previously_connected_client->clientid_len = 0;

                        esp_tls_server_session_delete(previously_connected_client->tls);
                        closesocket(sockst->client->sock);

                        if (sockst->client->clean_session)
                        {
                            sockst->client->del_subs();
                        }
                    }
                    sockst->client->clean_session = ret == mqtt_conn_return::clean_session ? true : false;
                    sockst->client->will = will;
                    sockst->client->client_state = Client::state::ready;
                    conn_ack(sockst->tls, 0x00, clean_session);
                    if (sockst->client->qued_msg_pack_id != 0)
                    {
                        sockst->client->client_state = Client::state::read_que;
                        msg.op = message::operation::write;
                        sendtoque(msg);
                    }else{
                        msg.op = message::operation::read;
                        sendtoque(msg);
                    }
                    continue;
                }
                if (sockst->client->client_state == Client::state::ready)
                {
                    if ((buf[0] & GET_CONTROL_PACKET_TYPE) == PUBLISH)
                    {
                        cur_pos = &buf[0];
                        uint8_t packet_qos = 0;

                        if (*cur_pos & DUPLICATION_FLAG)
                        { // dup flag
                            ESP_LOGI(TAG, "recieved duplicate publish");
                        }
                        if (*cur_pos & 0x06)
                        { // wrong, not allowed as it is reserved
                            ESP_LOGW(TAG, "not allowed");
                            drop_conn(msg, sockst, sock_to_clientid);
                            continue;
                        }
                        else if (*cur_pos & 0x04)
                        { // qos 2
                            packet_qos = 2;
                        }
                        else if (*cur_pos & 0x02)
                        { // qos 1
                            packet_qos = 1;
                        }
                        else
                        { // qos 0
                            packet_qos = 0;
                        }
                        bool retain = false;
                        if (*cur_pos++ & 0x01)
                        {
                            retain = true;
                        }

                        uint16_t topic_len = ntohs(*reinterpret_cast<uint16_t *>(cur_pos));
                        cur_pos += 2;
                        char *topic_ptr = reinterpret_cast<char *>(cur_pos);
                        cur_pos += topic_len;

                        uint16_t packetid = 0;
                        if (packet_qos > 0)
                        {
                            packetid = *reinterpret_cast<uint16_t *>(cur_pos);
                        }

                        if (retain)
                        {
                            myvector<uint8_t> tmp_msg(buf.begin(), buf.begin() + packet_length);
                            if (tmp_msg.size == packet_length)
                            {
                                myvector<uint8_t> *retained_msg = retained_sub_msg.get(topic_ptr, topic_len);
                                if (!retained_msg)
                                {
                                    retained_sub_msg.add(topic_ptr, topic_len, std::move(tmp_msg));
                                }
                                else
                                {
                                    *retained_msg = std::move(tmp_msg);
                                }
                            }
                        }
                        if (packet_qos == 1)
                        {
                            msg.op = message::operation::write;
                            if (sendtoque(msg))
                            {
                                add_packet_id(sockst, sock_cid::packet_state::sendpuback, packetid);
                            }
                        }
                        else if (packet_qos == 2)
                        {
                            msg.op = message::operation::write;
                            if (sendtoque(msg))
                            {
                                add_packet_id(sockst, sock_cid::packet_state::sendpubrec, packetid);
                            }
                        }else{
                            msg.op=message::operation::read;
                            sendtoque(msg);
                        }
                        publish_by_topic(topic_ptr, topic_len, buf.begin(), packet_length, packet_qos, packetid, sock_to_clientid);
                        continue;
                    }
                    else if (buf[0] == PUBACK)
                    {
                        uint16_t pubid_check = (buf[2] << 8) | buf[3];
                        bool found_packid = false;
                        for (auto &state_pair : sockst->packid_state)
                        {
                            if (state_pair.first == pubid_check && state_pair.second == sock_cid::packet_state::getpuback)
                            {
                                found_packid = true;
                                state_pair.first = 0;
                                break;
                            }
                        }
                        if (!found_packid)
                        {
                            drop_conn(msg, sockst, sock_to_clientid); // TODO: since not ignoring the dup packid packets it must be found
                            continue;
                        }
                        Publishhashmap::Node_pubmap *msg_info = publish_msg_store.get(pubid_check);
                        if (packet_length > 4 || buf[1] != 2)
                        {
                            if (!msg_info)
                            { // pretty radical but i am currently feeling like that and MQTT 3.1.1 docs state that if i can't send publish i can drop conn
                                drop_conn(msg, sockst, sock_to_clientid);
                                continue;
                            }
                            publish(msg_info->data.begin(), msg_info->data.size, msg_info->qos, msg_info->pubpack_id, sockst);
                            continue;
                        }
                        if (msg_info && --msg_info->count == 0)
                        {
                            publish_msg_store.erase(pubid_check);
                        }
                        msg.op=message::operation::read;
                        sendtoque(msg);
                        continue;
                    }
                    else if (buf[0] == PUBREC)
                    {
                        uint16_t pubid_check = (buf[2] << 8) | buf[3];
                        std::pair<uint16_t, sock_cid::packet_state> *packet_id_ptr = nullptr;
                        for (auto &state_pair : sockst->packid_state)
                        {
                            if (state_pair.first == pubid_check && state_pair.second == sock_cid::packet_state::getpubrec)
                            {
                                packet_id_ptr = &state_pair;
                                break;
                            }
                        }
                        if (!packet_id_ptr)
                        {
                            drop_conn(msg, sockst, sock_to_clientid); // TODO: since not ignoring the dup packid packets it must be found
                            continue;
                        }
                        Publishhashmap::Node_pubmap *msg_info = publish_msg_store.get(pubid_check);
                        if (packet_length > 4 || buf[1] != 2)
                        {
                            if (!msg_info)
                            { // pretty radical but i am currently feeling like that and MQTT 3.1.1 docs state that if i can't send publish i can drop conn
                                drop_conn(msg, sockst, sock_to_clientid);
                                continue;
                            }
                            publish(msg_info->data.begin(), msg_info->data.size, msg_info->qos, msg_info->pubpack_id, sockst);
                            continue;
                        }
                        if (msg_info && --msg_info->count == 0)
                        {
                            publish_msg_store.erase(pubid_check);
                        }
                        packet_id_ptr->second = sock_cid::packet_state::sendpubrel;
                        msg.op = message::operation::write;
                        sendtoque(msg);
                        continue;
                    }
                    else if (buf[0] == PUBCOMP)
                    {
                        if (packet_length > 4 || buf[1] != 2)
                        {
                            drop_conn(msg, sockst, sock_to_clientid); // TODO: since not ignoring the dup packid packets it must be found
                            continue;
                        }
                        uint16_t pubid_check = (buf[2] << 8) | buf[3];
                        std::pair<uint16_t, sock_cid::packet_state> *packet_id_ptr = nullptr;
                        for (auto &state_pair : sockst->packid_state)
                        {
                            if (state_pair.first == pubid_check && state_pair.second == sock_cid::packet_state::getpubcomp)
                            {
                                packet_id_ptr = &state_pair;
                                break;
                            }
                        }
                        if (!packet_id_ptr)
                        {
                            drop_conn(msg, sockst, sock_to_clientid); // TODO: since not ignoring the dup packid packets it must be found
                            continue;
                        }
                        packet_id_ptr->first = 0;
                        msg.op=message::operation::read;
                        sendtoque(msg);
                        continue;
                    }
                    else if (buf[0] == PUBREL)
                    {
                        if (packet_length> 4 || buf[1] != 2)
                        {
                            drop_conn(msg, sockst, sock_to_clientid); // TODO: since not ignoring the dup packid packets it must be found
                            continue;
                        }
                        uint16_t pubid_check = (buf[2] << 8) | buf[3];
                        std::pair<uint16_t, sock_cid::packet_state> *packet_id_ptr = nullptr;
                        for (auto &state_pair : sockst->packid_state)
                        {
                            if (state_pair.first == pubid_check && state_pair.second == sock_cid::packet_state::getpubrel)
                            {
                                packet_id_ptr = &state_pair;
                                break;
                            }
                        }
                        if (!packet_id_ptr)
                        {
                            drop_conn(msg, sockst, sock_to_clientid); // TODO: since not ignoring the dup packid packets it must be found
                            continue;
                        }
                        packet_id_ptr->second = sock_cid::packet_state::sendpubcomp;
                        msg.op = message::operation::write;
                        sendtoque(msg);
                        continue;
                    }
                    else if ((buf[0] & GET_CONTROL_PACKET_TYPE) == SUBSCRIBE)
                    {
                        ESP_LOGI(TAG, "subscribe recieved");

                        if (!((buf[0] & 0x0F) == SUBSCRIBE_RESERVED))
                        {
                            drop_conn(msg, sockst, sock_to_clientid);
                            continue;
                        }

                        myvector<uint8_t> suback;
                        if (!suback.reserve(5))
                        {
                            ESP_LOGW(TAG, "suback memory error");
                            drop_conn(msg, sockst, sock_to_clientid);
                            continue;
                        }
                        suback.push_back(0x90);
                        suback.push_back(0);          // for size below
                        suback.push_back(*cur_pos++); // packet id MSB
                        suback.push_back(*cur_pos++); // packet id LSB
                        packet_length -= additional_packet_len;
                        packet_length -= 2;

                        while (packet_length > 0)
                        {
                            uint16_t topic_len = ntohs(*reinterpret_cast<uint16_t *>(cur_pos));
                            cur_pos += 2;
                            packet_length -= (topic_len + 2);

                            char *topic_ptr = reinterpret_cast<char *>(cur_pos);
                            cur_pos += topic_len;

                            uint8_t req_qos = *cur_pos++;
                            packet_length--;
                            if (req_qos > 2)
                            {
                                req_qos = 2;
                            }
                            suback.push_back(sockst->client->subscribe(topic_ptr, topic_len, req_qos));
                            myvector<uint8_t> *retained_msg = retained_sub_msg.get(topic_ptr, topic_len);
                            if (retained_msg)
                            {
                                publish(retained_msg->begin(), retained_msg->size, 0, 0, sockst);
                            }
                        }

                        cur_pos = &suback[1]; // the zero added for size above
                        uint8_t insert_id = 2;
                        uint16_t sub_ack_len = suback.size - 2; // 4 is the size of type,packetid and one len field that are inserted before qos
                        do
                        {
                            *cur_pos = sub_ack_len % 128;
                            sub_ack_len /= 128;
                            if (sub_ack_len > 0)
                            {
                                *cur_pos |= 128;
                                if (!suback.insert(insert_id++, 0))
                                {
                                    len_error = true;
                                    ESP_LOGI(TAG, "suback len memory error");
                                    break;
                                }
                            }
                            cur_pos++;
                        } while (sub_ack_len > 0);
                        if (len_error)
                        {
                            drop_conn(msg, sockst, sock_to_clientid);
                            continue;
                        }
                        esp_tls_conn_write(sockst->tls, suback.begin(), suback.size);
                        ESP_LOGI(TAG, "suback sent");
                        msg.op=message::operation::read;
                        sendtoque(msg);
                        continue;
                    }
                    else if ((buf[0] & GET_CONTROL_PACKET_TYPE) == UNSUBSCRIBE)
                    {
                        ESP_LOGI(TAG, "unsubscribe recieved");
                        if (!((buf[0] & 0x0F) == SUBSCRIBE_RESERVED))
                        {
                            ESP_LOGW(TAG, "malformed reserved bits");
                            drop_conn(msg, sockst, sock_to_clientid);
                        }

                        std::array<uint8_t, 4> unsuback;
                        unsuback[0] = UNSUBACK;
                        unsuback[1] = 2;          // pack type and len
                        unsuback[3] = *cur_pos++; // packet id (MSB)
                        unsuback[4] = *cur_pos++; //(LSB)
                        packet_length -= additional_packet_len;
                        packet_length -= 2;

                        while (packet_length > 0)
                        { // TODO: might be wrong
                            uint16_t topic_len = ntohs(*reinterpret_cast<uint16_t *>(cur_pos));
                            cur_pos += 2;
                            packet_length -= (topic_len + 2);
                            char *topic_ptr = reinterpret_cast<char *>(cur_pos);
                            cur_pos += topic_len;
                            sockst->client->unsubscribe(topic_ptr, topic_len);
                        }

                        esp_tls_conn_write(sockst->tls, unsuback.begin(), unsuback.size());
                        ESP_LOGI(TAG, "unsuback sent");
                        msg.op=message::operation::read;
                        sendtoque(msg);
                        continue;
                    }
                    else if ((buf[0] & GET_CONTROL_PACKET_TYPE) == PINGRESP)
                    {
                        std::array<uint8_t, 2> pingresp;
                        pingresp[0] = 0xD0;
                        pingresp[1] = 0x00;
                        esp_tls_conn_write(sockst->tls, pingresp.begin(), pingresp.size());
                        ESP_LOGI(TAG, "pingresp sent");
                        msg.op=message::operation::read;
                        sendtoque(msg);
                        continue;
                    }
                    else if ((buf[0] & GET_CONTROL_PACKET_TYPE) == DISCONNECT)
                    {
                        sockst->client->clean_session = true;
                        drop_conn(msg, sockst, sock_to_clientid, false);
                        continue;
                    }
                }
            }
            else if (msg.op == message::operation::write)
            {
                if (sockst->client->client_state == Client::state::read_que)
                {
                    sockst->client->client_state = Client::state::ready;
                    msg.op = message::operation::read;
                    sendtoque(msg);
                    Publishhashmap::Node_pubmap *node = publish_msg_store.get(sockst->client->qued_msg_pack_id);
                    if (node)
                    {
                        publish(node->data.begin(), node->data.size, node->qos, node->pubpack_id, sockst);
                        if (--node->count == 0)
                        {
                            publish_msg_store.erase(sockst->client->qued_msg_pack_id);
                        }
                    }
                    sockst->client->qued_msg_pack_id = 0;
                }
                else if (sockst->client->client_state == Client::state::ready)
                {
                    std::pair<uint16_t, sock_cid::packet_state> *packetid_ptr = nullptr;
                    for (auto &state_pair : sockst->packid_state)
                    {
                        if (state_pair.first != 0 && (state_pair.second == sock_cid::packet_state::sendpuback || state_pair.second == sock_cid::packet_state::sendpubcomp || state_pair.second == sock_cid::packet_state::sendpubrec || state_pair.second == sock_cid::packet_state::sendpubrel || state_pair.second == sock_cid::packet_state::sendpublish))
                        {
                            packetid_ptr = &state_pair;
                            break;
                        }
                    }
                    if (!packetid_ptr)
                    {
                        continue;
                    }
                    if (packetid_ptr->second == sock_cid::packet_state::sendpuback)
                    {
                        std::array<uint8_t, 4> puback;
                        puback[0] = PUBACK;
                        puback[1] = 2;
                        puback[2] = (packetid_ptr->first >> 8) & 0xFF;
                        puback[3] = packetid_ptr->first & 0xFF;
                        esp_tls_conn_write(sockst->tls, puback.begin(), 4);
                        packetid_ptr->first = 0;
                        msg.op=message::operation::read;
                        sendtoque(msg);
                        continue;
                    }
                    else if (packetid_ptr->second == sock_cid::packet_state::sendpubrec)
                    {
                        std::array<uint8_t, 4> pubrec;
                        pubrec[0] = PUBREC;
                        pubrec[1] = 2;
                        pubrec[2] = (packetid_ptr->first >> 8) & 0xFF;
                        pubrec[3] = packetid_ptr->first & 0xFF;
                        esp_tls_conn_write(sockst->tls, pubrec.begin(), 4);
                        packetid_ptr->second = sock_cid::packet_state::getpubrel;
                        msg.op=message::operation::read;
                        sendtoque(msg);
                        continue;
                    }
                    else if (packetid_ptr->second == sock_cid::packet_state::sendpubrel)
                    {
                        std::array<uint8_t, 4> pubrel;
                        pubrel[0] = PUBREL;
                        pubrel[1] = 2;
                        pubrel[2] = (packetid_ptr->first >> 8) & 0xFF;
                        pubrel[3] = packetid_ptr->first & 0xFF;
                        esp_tls_conn_write(sockst->tls, pubrel.begin(), 4);
                        packetid_ptr->second = sock_cid::packet_state::getpubcomp;
                        msg.op=message::operation::read;
                        sendtoque(msg);
                        continue;
                    }
                    else if (packetid_ptr->second == sock_cid::packet_state::sendpubcomp)
                    {
                        std::array<uint8_t, 4> pubcomp;
                        pubcomp[0] = PUBCOMP;
                        pubcomp[1] = 2;
                        pubcomp[2] = (packetid_ptr->first >> 8) & 0xFF;
                        pubcomp[3] = packetid_ptr->first & 0xFF;
                        esp_tls_conn_write(sockst->tls, pubcomp.begin(), 4);
                        packetid_ptr->first = 0;
                        msg.op=message::operation::read;
                        sendtoque(msg);
                        continue;
                    }
                    else if (packetid_ptr->second == sock_cid::packet_state::sendpublish)
                    {
                        Publishhashmap::Node_pubmap *node = publish_msg_store.get(packetid_ptr->first);
                        if (node)
                        {
                            publish(node->data.begin(), node->data.size, node->qos, packetid_ptr->first, sockst);
                            if (--node->count == 0)
                            {
                                publish_msg_store.erase(packetid_ptr->first);
                            }
                        }
                        msg.op=message::operation::read;
                        sendtoque(msg);
                    }
                }
            }
        }
    }
}

static mqtt_conn_return connect_mqtt(std::array<uint8_t, BUFF_SIZE> &buf, uint8_t* cur_pos, sock_cid *sockst, Client::Will &will)
{
    uint8_t packet_type = (buf[0] & 0xF0) >> 4;
    uint8_t packet_flags = (buf[0] & 0x0F);
    uint8_t conn_flags = 0;
    uint16_t keepalv_sec = 0;

    uint16_t clientid_len = 0;

    bool clean_session = true;
    bool username = false;
    bool password = false;

    if (!(packet_type == 0x01))
    {
        ESP_LOGW(TAG, "missed it %u", buf[0]);
    }

    if (packet_flags != 0x00)
    {
        ESP_LOGE(TAG, "wrong flags: %u", buf[0]);
        return disconnect;
    }

    if (ntohs(*(uint16_t *)cur_pos) != 4)
    {
        ESP_LOGW(TAG, "protocol length is wrong");
        return disconnect;
    }
    cur_pos += 2;

    if (*cur_pos != 'M' || *(cur_pos + 1) != 'Q' || *(cur_pos + 2) != 'T' || *(cur_pos + 3) != 'T')
    {
        ESP_LOGW(TAG, "name isnt MQTT");
        return disconnect;
    }
    cur_pos += 4;
    if (*cur_pos++ != 0x04)
    {
        // for MQTT 3.1.1 CONNACK with return code 0x01 and then disconnect
        conn_ack(sockst->tls, 0x01, 1);
        ESP_LOGW(TAG, "wrong protocol version");
        return disconnect;
    }
    conn_flags = *cur_pos++;
    if (conn_flags & 0x01)
    {
        ESP_LOGW(TAG, "reserved flag of connect flags is set");
        return disconnect;
    }
    if (!(conn_flags & 0x02))
    {
        clean_session = false;
    }
    if (conn_flags & 0x04)
    {
        will.presence_flag = true;
        will.retain = conn_flags & 0x20;
        will.qos = (conn_flags >> 3) & 0x03;
        if (will.qos > 2)
        {
            will.qos = 2;
        }
    }
    else if (conn_flags & 0x08 || conn_flags & 0x10 || conn_flags & 0x20)
    {
        ESP_LOGW(TAG, "wrong will flags");
        return disconnect;
    }

    if (conn_flags & 0x80)
    {
        username = true;
    }
    if (conn_flags & 0x40)
    {
        password = true;
    }
    keepalv_sec = ntohs(*reinterpret_cast<uint16_t *>(cur_pos)) * 2; //*2 for simplicity instead of 1.5 times
    cur_pos += 2;

    clientid_len = ntohs(*reinterpret_cast<uint16_t *>(cur_pos));
    cur_pos += 2;

    if (clientid_len < 1 || clientid_len > 23)
    { // TODO: in future just assign if its empty
        return disconnect;
    }
    else
    {
        std::copy(reinterpret_cast<char *>(cur_pos), reinterpret_cast<char *>(cur_pos + clientid_len), sockst->clientid.begin());
        cur_pos += clientid_len;
        sockst->clientid_len = clientid_len;
    }

    if (will.presence_flag)
    {
        uint16_t will_len = ntohs(*reinterpret_cast<uint16_t *>(cur_pos));
        cur_pos += 2;
        will.topic = myvector<char>(reinterpret_cast<char *>(cur_pos), reinterpret_cast<char *>(cur_pos) + will_len);
        if (will.topic.size != will_len)
        {
            ESP_LOGW(TAG, "will topic memory error");
            return disconnect;
        }
        cur_pos += will_len;

        will_len = ntohs(*reinterpret_cast<uint16_t *>(cur_pos));
        cur_pos += 2;
        will.msg = myvector<uint8_t>(cur_pos, cur_pos + will_len);
        if (will.msg.size != will_len)
        {
            ESP_LOGW(TAG, "will message memory error");
            return disconnect;
        }
        cur_pos += will_len;
    }

    if (username)
    {
        uint16_t username_len = ntohs(*reinterpret_cast<uint16_t *>(cur_pos));
        cur_pos += 2;
        char *username_ptr = reinterpret_cast<char *>(cur_pos);
        cur_pos += username_len;
        uint16_t pass_len = 0;
        if (password)
        {
            pass_len = ntohs(*reinterpret_cast<uint16_t *>(cur_pos));
            cur_pos += 2;
        }
        u_char *pass_ptr = cur_pos;
        bool correct_pass = user_cred.check_password(username_ptr, username_len, pass_ptr, pass_len);
        if (!correct_pass)
            return disconnect;
        cur_pos += pass_len;
    }
    if (clean_session)
        return mqtt_conn_return::clean_session;
    return unclean_session;

}