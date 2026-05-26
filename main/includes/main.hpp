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
#include "certs.hpp"
#include "wificonfig.hpp"

#define CONFIG_EXAMPLE_IPV4
#define PORT 8001
#define KEEPALIVE_IDLE 120
#define KEEPALIVE_INTERVAL 30
#define KEEPALIVE_COUNT 5

