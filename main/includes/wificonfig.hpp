#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "wificredentials.hpp"

#define EXAMPLE_ESP_MAXIMUM_RETRY 10
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

constexpr char IP_ADDR[] = "192.168.1.10";
constexpr char NETMASK_ADDR[] = "255.255.255.0";
constexpr char GATEWAY_ADDR[] = "192.168.1.1";

esp_err_t wifi_init_sta(void);