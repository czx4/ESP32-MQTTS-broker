#pragma once
#include "myhashmap.hpp"
#include "esp_random.h"
#include "mbedtls/sha256.h"
#include "esp_log.h"

class user_creds
{
public:
    struct Password
    {
        bool protection = false;
        std::array<uint8_t, 16> salt;
        std::array<uint8_t, 32> hash;
    };
    user_creds(std::size_t cap);
    ~user_creds();
    bool add_user(char *username_ptr, std::size_t username_len);
    bool add_user(char *username_ptr, std::size_t username_len, u_char *pass_ptr, std::size_t pass_len);
    bool change_password(char *username_ptr, std::size_t username_len, u_char *pass_ptr, std::size_t pass_len);
    void delete_user(char *username_ptr, std::size_t username_len);
    bool check_password(char *username_ptr, std::size_t username_len, u_char *pass_ptr, std::size_t pass_len);

private:
    myhashmap<Password *> data;
};