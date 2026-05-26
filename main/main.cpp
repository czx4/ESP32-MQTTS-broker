#include "main.hpp"

static constexpr const char *TAG = "main";
constexpr static uint32_t TASK_SIZE = 10 * 1024;

static void tcp_server_task(void *pvParameters)
{
    constexpr std::size_t MAX_ERRORS_BEFORE_RESTART = 10;
    constexpr std::size_t TIME_BETWEEN_TRIES_AFTER_ERROR = 500;
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    int error_count = 0;
    // int keepAlive = 1;
    // int keepIdle = KEEPALIVE_IDLE;
    // int keepInterval = KEEPALIVE_INTERVAL;
    // int keepCount = KEEPALIVE_COUNT;
    struct sockaddr_storage dest_addr;
    esp_tls_cfg_server tls_cfg{};
    tls_cfg.cacert_buf = NULL;
    tls_cfg.cacert_bytes = 0;
    tls_cfg.servercert_buf = pem_cert;
    tls_cfg.servercert_bytes = sizeof(pem_cert);
    tls_cfg.serverkey_buf = pem_prv_key;
    tls_cfg.serverkey_bytes = sizeof(pem_prv_key);

#ifdef CONFIG_EXAMPLE_IPV4
    if (addr_family == AF_INET)
    {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(PORT);
        ip_protocol = IPPROTO_IP;
    }
#endif

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    while (listen_sock < 0 && error_count < MAX_ERRORS_BEFORE_RESTART)
    {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        ++error_count;
        vTaskDelay(TIME_BETWEEN_TRIES_AFTER_ERROR / portTICK_PERIOD_MS);
        listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    }
    if (error_count >= MAX_ERRORS_BEFORE_RESTART)
    {
        ESP_LOGW(TAG, "Reached max tries of creating socket before restart");
        esp_restart();
    }
    else
        error_count = 0;
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    while (err != 0 && error_count < MAX_ERRORS_BEFORE_RESTART)
    {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        ++error_count;
        vTaskDelay(TIME_BETWEEN_TRIES_AFTER_ERROR / portTICK_PERIOD_MS);
        err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    }
    if (error_count >= MAX_ERRORS_BEFORE_RESTART)
    {
        ESP_LOGW(TAG, "Reached max tries of binding socket before restart");
        esp_restart();
    }
    else
        error_count = 0;

    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    err = listen(listen_sock, 1);
    while (err != 0 && error_count < MAX_ERRORS_BEFORE_RESTART)
    {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        ++error_count;
        vTaskDelay(TIME_BETWEEN_TRIES_AFTER_ERROR / portTICK_PERIOD_MS);
        err = listen(listen_sock, 1);
    }
    if (error_count >= MAX_ERRORS_BEFORE_RESTART)
    {
        ESP_LOGW(TAG, "Reached max tries of trying to listen on socket before restart");
        esp_restart();
    }
    else
        error_count = 0;

    while (true)
    {
        // TODO: add delay if low on mem to not hog cpu
        ESP_LOGI(TAG, "Socket listening");
        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0)
        {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            continue;
        }

        // // Set tcp keepalive option
        struct timeval initial_timeout;
        initial_timeout.tv_sec = 60;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &initial_timeout, sizeof(struct timeval));
        // setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int)); //TODO: organize and delete not used
        // setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        // setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        // setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
        // Convert ip address to string
        esp_tls_t *tls = esp_tls_init();
        if (!tls)
        {
            ESP_LOGE(TAG, "TLS init allocation failed");
            closesocket(sock);
            continue;
        }
        if (esp_tls_server_session_create(&tls_cfg, sock, tls) == 0)
        {
            ESP_LOGI(TAG, "TLS handshake successful");
        }
        else
        {
            ESP_LOGE(TAG, "TLS handshake failed");
            esp_tls_server_session_delete(tls);
            closesocket(sock);
            continue;
        }
#ifdef CONFIG_EXAMPLE_IPV4
        if (source_addr.ss_family == PF_INET)
        {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }
#endif

        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);
        char buffer[]="hello world\0";
        esp_tls_conn_write(tls,buffer,sizeof(buffer));
        esp_tls_server_session_delete(tls);
        closesocket(sock);
    }
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(wifi_init_sta());
#ifdef CONFIG_EXAMPLE_IPV4
    xTaskCreate(tcp_server_task, "tcp_server", 6 * 1024, (void *)AF_INET, 0, NULL); // TODO: adjust size
#endif
}