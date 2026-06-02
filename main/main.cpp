#include "main.hpp"

static constexpr const char *TAG = "main";
constexpr static uint32_t TASK_SIZE = 10 * 1024;

QueueHandle_t pending_socks, socks_for_fds;

void error_check(int &error_count, const char ptr[])
{
    constexpr std::size_t MAX_ERRORS_BEFORE_RESTART = 10;
    if (error_count >= MAX_ERRORS_BEFORE_RESTART)
    {
        ESP_LOGW(TAG, "Reached max error tries while %s", ptr);
        esp_restart();
    }
    else
        error_count = 0;
}

static void tcp_server_task(void *pvParameters)
{
    constexpr std::size_t MAX_ERRORS_BEFORE_RESTART = 10;
    constexpr std::size_t TIME_BETWEEN_TRIES_AFTER_ERROR = 500;
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    int error_count = 0;
    struct sockaddr_storage dest_addr;

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
    error_check(error_count, "creating socket");

    int flags = fcntl(listen_sock, F_GETFL);
    while (flags == -1 && error_count < MAX_ERRORS_BEFORE_RESTART)
    {
        ESP_LOGE(TAG, "error in getting socket flags");
        ++error_count;
        vTaskDelay(TIME_BETWEEN_TRIES_AFTER_ERROR / portTICK_PERIOD_MS);
        flags = fcntl(listen_sock, F_GETFL);
    }
    error_check(error_count, "getting socket flags");

    while (fcntl(listen_sock, F_SETFL, flags | O_NONBLOCK) == -1 && error_count < MAX_ERRORS_BEFORE_RESTART)
    {
        ESP_LOGE(TAG, "error in setting socket flags");
        ++error_count;
        vTaskDelay(TIME_BETWEEN_TRIES_AFTER_ERROR / portTICK_PERIOD_MS);
    }
    error_check(error_count, "setting socket flags");

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
    error_check(error_count, "binding socket");

    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    err = listen(listen_sock, 1);
    while (err != 0 && error_count < MAX_ERRORS_BEFORE_RESTART)
    {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        ++error_count;
        vTaskDelay(TIME_BETWEEN_TRIES_AFTER_ERROR / portTICK_PERIOD_MS);
        err = listen(listen_sock, 1);
    }
    error_check(error_count, "listening to socket");

    uint16_t max_socket = listen_sock;
    fd_set master_readfds;
    fd_set master_writefds;

    fd_set readfds;
    fd_set writefds;

    FD_ZERO(&master_readfds);
    FD_ZERO(&master_writefds);
    FD_SET(listen_sock, &master_readfds);
    while (true)
    {
        readfds = master_readfds;
        writefds = master_writefds;
        struct timeval tv{0, 0};
        select(max_socket + 1, &readfds, &writefds, NULL, &tv);
        if (FD_ISSET(listen_sock, &readfds))
        {
            struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
            socklen_t addr_len = sizeof(source_addr);
            int new_sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
            if (new_sock > 63)
            {
                ESP_LOGI(TAG, "max socket number reached");
                closesocket(new_sock);
            }
            else if (new_sock != -1)
            {
                if (new_sock > max_socket)
                    max_socket = new_sock;
                message accepted_sock(new_sock, message::operation::write);
                xQueueSendToBack(pending_socks, &accepted_sock, 0);
            }
        }
        for (int sock = 0; sock <= max_socket; ++sock)
        {
            if (sock == listen_sock)
                continue;
            if (FD_ISSET(sock, &readfds))
            {
                message msg(sock, message::operation::read);
                xQueueSendToBack(pending_socks, &msg, 0);
                FD_CLR(msg.socket, &master_readfds);
            }
            if (FD_ISSET(sock, &writefds))
            {
                message msg(sock, message::operation::write);
                xQueueSendToBack(pending_socks, &msg, 0);
                FD_CLR(msg.socket, &master_writefds);
            }
        }
        message rec_msg(-1, message::operation::read);
        while (xQueueReceive(socks_for_fds, &rec_msg, 0) == pdPASS)
        {
            if (rec_msg.op == message::operation::read)
            {
                FD_SET(rec_msg.socket, &master_readfds);
            }
            else if (rec_msg.op == message::operation::write)
            {
                FD_SET(rec_msg.socket, &master_writefds);
            }
            else if (rec_msg.op == message::operation::delread)
            {
                FD_CLR(rec_msg.socket, &master_readfds);
            }
            else if (rec_msg.op == message::operation::delall)
            {
                FD_CLR(rec_msg.socket, &master_readfds);
                FD_CLR(rec_msg.socket, &master_writefds);
            }
        }
    }
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(wifi_init_sta());
#ifdef CONFIG_EXAMPLE_IPV4
    pending_socks = xQueueCreate(10, sizeof(message)); // TODO: adjust size
    socks_for_fds = xQueueCreate(10, sizeof(message)); // TODO: adjust size
    if (!pending_socks || !socks_for_fds)
    {
        ESP_LOGW(TAG, "failed to create queue");
        esp_restart();
    }
    xTaskCreate(tcp_server_task, "tcp_server", 6 * 1024, (void *)AF_INET, 0, NULL); // TODO: adjust size
    xTaskCreate(worker, "worker", TASK_SIZE, NULL, 0, NULL);
#endif
}