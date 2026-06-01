#include "user_creds.hpp"

static constexpr const char *TAG = "user_creds";

user_creds::user_creds(std::size_t cap = 1) : data(cap) {}
user_creds::~user_creds() {}

bool user_creds::add_user(char *username_ptr, std::size_t username_len)
{

    Password *new_pass = new (std::nothrow) Password;
    if (!new_pass)
    {
        ESP_LOGI(TAG, "password allocation failed");
        return false;
    }

    return data.add(username_ptr, username_len, std::move(new_pass));
}

bool user_creds::add_user(char *username_ptr, std::size_t username_len, u_char *pass_ptr, std::size_t pass_len)
{

    Password *new_pass = new (std::nothrow) Password;
    if (!new_pass)
    {
        ESP_LOGI(TAG, "password allocation failed");
        return false;
    }
    esp_fill_random(new_pass->salt.begin(), new_pass->salt.size());
    new_pass->protection = true;

    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    int mbedcode = mbedtls_sha256_starts(&sha_ctx, 0);
    if (mbedcode != 0)
    {
        mbedtls_sha256_free(&sha_ctx);
        ESP_LOGW(TAG, "mbedtls start error: %i", mbedcode);
        return false;
    }

    mbedcode = mbedtls_sha256_update(&sha_ctx, pass_ptr, pass_len);
    if (mbedcode != 0)
    {
        mbedtls_sha256_free(&sha_ctx);
        ESP_LOGW(TAG, "mbedtls update error: %i", mbedcode);
        return false;
    }

    mbedcode = mbedtls_sha256_update(&sha_ctx, new_pass->salt.begin(), new_pass->salt.size());
    if (mbedcode != 0)
    {
        mbedtls_sha256_free(&sha_ctx);
        ESP_LOGW(TAG, "mbedtls update error: %i", mbedcode);
        return false;
    }

    mbedcode = mbedtls_sha256_finish(&sha_ctx, new_pass->hash.begin());
    if (mbedcode != 0)
    {
        mbedtls_sha256_free(&sha_ctx);
        ESP_LOGW(TAG, "mbedtls finish error: %i", mbedcode);
        return false;
    }

    mbedtls_sha256_free(&sha_ctx);
    return data.add(username_ptr, username_len, std::move(new_pass));
}

bool user_creds::change_password(char *username_ptr, std::size_t username_len, u_char *pass_ptr, std::size_t pass_len)
{
    Password *pass_to_change = *data.get(username_ptr, username_len);
    if (!pass_to_change)
    {
        ESP_LOGI(TAG, "password lookup failed");
        return false;
    }

    if (pass_len == 0)
    {
        pass_to_change->protection = false;
        return true;
    }

    esp_fill_random(pass_to_change->salt.begin(), pass_to_change->salt.size());
    pass_to_change->protection = true;

    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    int mbedcode = mbedtls_sha256_starts(&sha_ctx, 0);
    if (mbedcode != 0)
    {
        mbedtls_sha256_free(&sha_ctx);
        ESP_LOGW(TAG, "mbedtls start error: %i", mbedcode);
        return false;
    }

    mbedcode = mbedtls_sha256_update(&sha_ctx, pass_ptr, pass_len);
    if (mbedcode != 0)
    {
        mbedtls_sha256_free(&sha_ctx);
        ESP_LOGW(TAG, "mbedtls update error: %i", mbedcode);
        return false;
    }

    mbedcode = mbedtls_sha256_update(&sha_ctx, pass_to_change->salt.begin(), pass_to_change->salt.size());
    if (mbedcode != 0)
    {
        mbedtls_sha256_free(&sha_ctx);
        ESP_LOGW(TAG, "mbedtls update error: %i", mbedcode);
        return false;
    }

    mbedcode = mbedtls_sha256_finish(&sha_ctx, pass_to_change->hash.begin());
    if (mbedcode != 0)
    {
        mbedtls_sha256_free(&sha_ctx);
        ESP_LOGW(TAG, "mbedtls finish error: %i", mbedcode);
        return false;
    }

    mbedtls_sha256_free(&sha_ctx);
    return true;
}
void user_creds::delete_user(char *username_ptr, std::size_t username_len)
{
    data.erase(username_ptr, username_len);
}

bool user_creds::check_password(char *username_ptr, std::size_t username_len, u_char *pass_ptr, std::size_t pass_len)
{
    Password *correct_pass = *data.get(username_ptr, username_len);
    if (!correct_pass)
    {
        ESP_LOGI(TAG, "password lookup failed");
        return false;
    }

    if (pass_len == 0)
    {
        return !correct_pass->protection;
    }

    Password *pass_to_check = new (std::nothrow) Password;
    if (!pass_to_check)
    {
        ESP_LOGI(TAG, "password allocation failed");
        return false;
    }

    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    int mbedcode = mbedtls_sha256_starts(&sha_ctx, 0);
    if (mbedcode != 0)
    {
        mbedtls_sha256_free(&sha_ctx);
        ESP_LOGW(TAG, "mbedtls start error: %i", mbedcode);
        delete pass_to_check;
        return false;
    }

    mbedcode = mbedtls_sha256_update(&sha_ctx, pass_ptr, pass_len);
    if (mbedcode != 0)
    {
        mbedtls_sha256_free(&sha_ctx);
        ESP_LOGW(TAG, "mbedtls update error: %i", mbedcode);
        delete pass_to_check;
        return false;
    }

    mbedcode = mbedtls_sha256_update(&sha_ctx, correct_pass->salt.begin(), correct_pass->salt.size());
    if (mbedcode != 0)
    {
        mbedtls_sha256_free(&sha_ctx);
        ESP_LOGW(TAG, "mbedtls update error: %i", mbedcode);
        delete pass_to_check;
        return false;
    }

    mbedcode = mbedtls_sha256_finish(&sha_ctx, pass_to_check->hash.begin());
    if (mbedcode != 0)
    {
        mbedtls_sha256_free(&sha_ctx);
        ESP_LOGW(TAG, "mbedtls finish error: %i", mbedcode);
        delete pass_to_check;
        return false;
    }

    mbedtls_sha256_free(&sha_ctx);
    bool response = (pass_to_check->hash == correct_pass->hash);
    delete pass_to_check;
    return response;
}