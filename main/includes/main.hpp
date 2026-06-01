#pragma once
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include <limits>
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include <esp_tls.h>
#include <memory>
#include "wificonfig.hpp"
#include "worker.hpp"

#define CONFIG_EXAMPLE_IPV4
#define PORT 8001
#define KEEPALIVE_IDLE 120
#define KEEPALIVE_INTERVAL 30
#define KEEPALIVE_COUNT 5

extern QueueSetHandle_t pending_socks, socks_for_fds;

class message
{
public:
    enum operation : uint8_t
    {
        write,
        read,
        delread,
        delall
    };
    int socket;
    operation op;
    message(int s, operation o) : socket(s), op(o) {}
};
