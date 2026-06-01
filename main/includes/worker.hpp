#pragma once

// #include "main.hpp"
#include "Client.hpp"
#include "certs.hpp"
#include "user_creds.hpp"
#include "Publishhashmap.hpp"

class Client;

constexpr uint8_t GET_CONTROL_PACKET_TYPE = 0xF0;
constexpr uint8_t PUBLISH = 0x30;
constexpr uint8_t SUBSCRIBE = 0x80;
constexpr uint8_t SUBSCRIBE_RESERVED = 0x02;
constexpr uint8_t UNSUBSCRIBE = 0xA0;
constexpr uint8_t UNSUBACK = 0xB0;
constexpr uint8_t PINGRESP = 0xC0;
constexpr uint8_t DISCONNECT = 0xE0;

constexpr uint8_t PUBREL = 0x62;
constexpr uint8_t PUBREC = 0x50;
constexpr uint8_t PUBCOMP = 0x70;
constexpr uint8_t PUBACK = 0x40;
constexpr uint8_t DUPLICATION_FLAG = 0x08;

enum mqtt_conn_return
{
    disconnect,
    clean_session,
    unclean_session
};

struct sock_cid
{
    enum phase
    {
        uninit,
        tls_handshake,
        mqtt
    };
    enum packet_state
    {
        getpuback,
        getpubrec,
        getpubrel,
        getpubcomp,
        sendpublish,
        sendpuback,
        sendpubrec,
        sendpubrel,
        sendpubcomp
    };
    Client *client = nullptr;
    std::array<char, 24> clientid;
    uint8_t clientid_len;
    phase phs;
    esp_tls_t *tls;
    myvector<std::pair<uint16_t, packet_state>> packid_state;
    sock_cid()
    {
        clientid_len = 0;
        phs = phase::uninit;
        tls = nullptr;
        packid_state.reserve(2);
    }
};

void worker(void *args);