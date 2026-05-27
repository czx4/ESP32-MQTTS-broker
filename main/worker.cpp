#include "worker.hpp"

constexpr int BUFFER_SIZE = 1024;
static constexpr const char *TAG = "worker";

bool sendtoque(message &msg)
{
    if (xQueueSendToBack(socks_for_fds, &msg, portMAX_DELAY) != pdPASS) // TODO set some kind of timeout
    {
        ESP_LOGI(TAG, "failed to send to socks_for_fds");
        return false;
    }
    return true;
}

void worker(void *args)
{
    Client client_store[64];
    message msg(-1, message::operation::read);
    std::array<uint8_t, BUFFER_SIZE> buf;

    esp_tls_cfg_server tls_cfg{};
    tls_cfg.cacert_buf = NULL;
    tls_cfg.cacert_bytes = 0;
    tls_cfg.servercert_buf = pem_cert;
    tls_cfg.servercert_bytes = sizeof(pem_cert);
    tls_cfg.serverkey_buf = pem_prv_key;
    tls_cfg.serverkey_bytes = sizeof(pem_prv_key);
    while (true)
    {
        while (xQueueReceive(pending_socks, &msg, portMAX_DELAY) == pdPASS)
        {
            if (msg.socket == -1)
                break;
            int sock = msg.socket;
            Client *client = &client_store[sock];
            int code=-1;
            char test[]="hello world ";
            switch (client->client_state)
            {
            case Client::state::uninit:
                client->m_tls = esp_tls_init();
                if (!client->m_tls)
                {
                    ESP_LOGE(TAG, "TLS init allocation failed");
                    closesocket(msg.socket);
                    break;
                }
                esp_tls_server_session_init(&tls_cfg, sock, client->m_tls);
                code = esp_tls_server_session_continue_async(client->m_tls);
                if (code < 0)
                {
                    esp_tls_server_session_delete(client->m_tls);
                    closesocket(sock);
                    break;
                }
                else if (code == 0)
                {
                    client->client_state = Client::state::ready;
                    msg.op=message::read;
                    sendtoque(msg);
                    esp_tls_conn_write(client->m_tls,&test,sizeof(test));
                    break;
                }
                else if (code == ESP_TLS_ERR_SSL_WANT_WRITE)
                {
                    msg.op = message::operation::write;
                }
                else if(code==ESP_TLS_ERR_SSL_WANT_READ){
                    msg.op=message::operation::read;
                }
                sendtoque(msg);
                client->client_state = Client::state::processing_handshake;
                break;
            case Client::state::processing_handshake:
                code=esp_tls_server_session_continue_async(client->m_tls);
                if(code<0){
                    esp_tls_server_session_delete(client->m_tls);
                    closesocket(sock);
                    msg.op=message::operation::delall;
                }else if(code==0){
                    msg.op=message::operation::delwrite;
                    sendtoque(msg);
                    msg.op=message::operation::read;
                    client->client_state=Client::state::ready;
                    esp_tls_conn_write(client->m_tls,&test,sizeof(test));
                }else if(code==ESP_TLS_ERR_SSL_WANT_WRITE){
                    msg.op=message::operation::delread;
                    sendtoque(msg);
                    msg.op = message::operation::write;
                }else if(code==ESP_TLS_ERR_SSL_WANT_READ){
                    msg.op=message::operation::delwrite;
                    sendtoque(msg);
                    msg.op=message::operation::read;
                }
                sendtoque(msg);
                break;
            case Client::state::ready:
                esp_tls_conn_write(client->m_tls,&test,sizeof(test));
                break;
            default:
                esp_tls_server_session_delete(client->m_tls);
                closesocket(sock);
                msg.op=message::operation::delall;
                sendtoque(msg);
                break;
            }
        }
    }

    //     if (FD_ISSET(listen_sock, &readfds))
    //     {
    //         struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
    //         socklen_t addr_len = sizeof(source_addr);
    //         int new_sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);

    //         tls = esp_tls_init();
    //         if (!tls)
    //         {
    //             ESP_LOGE(TAG, "TLS init allocation failed");
    //             closesocket(new_sock);
    //             continue;
    //         }
    //         if (esp_tls_server_session_create(&tls_cfg, new_sock, tls) == 0)
    //         {
    //             ESP_LOGI(TAG, "TLS handshake successful");
    //         }
    //         else
    //         {
    //             ESP_LOGE(TAG, "TLS handshake failed");
    //             esp_tls_server_session_delete(tls);
    //             closesocket(new_sock);
    //             continue;
    //         }
    // #ifdef CONFIG_EXAMPLE_IPV4
    //         if (source_addr.ss_family == PF_INET)
    //         {
    //             inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
    //         }
    // #endif

    //         ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);
    //         char buffer[] = "hello world";
    //         esp_tls_conn_write(tls, buffer, sizeof(buffer));
    //         // esp_tls_server_session_delete(tls);
    //         // closesocket(new_sock);
    //         if (new_sock > max_socket)
    //             max_socket = new_sock;
    //         FD_SET(new_sock, &master_writefds);
    //     }
    //     for (int i = 0; i <= max_socket; ++i)
    //     { // TODO: move allat to worker thread, implement server session async
    //         if (FD_ISSET(i, &writefds))
    //         {
    //             char buf[] = "crazy ";
    //             int code = esp_tls_conn_write(tls, buf, sizeof(buf));
    //             if (code < 0)
    //             {
    //                 esp_tls_server_session_delete(tls);
    //                 closesocket(i);
    //                 FD_CLR(i, &master_writefds);
    //             }
    //         }
    //     }
}