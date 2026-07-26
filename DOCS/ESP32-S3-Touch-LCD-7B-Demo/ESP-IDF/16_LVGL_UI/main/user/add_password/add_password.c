#include <stdio.h>
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_random.h"  // Include the declaration for esp_random()
#include "mbedtls/sha256.h"

#include "ui.h"

#include "add_password.h"

// Initialize NVS (Non-Volatile Storage)
esp_err_t init_nvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Erase and re-initialize if no free pages or new version found
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    // Open NVS for reading
    nvs_handle_t nvs_handle;
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        printf("Error opening NVS handle!\n");
        return false;
    }

    // Read username from NVS
    size_t username_size = MAX_LENGTH;
    err = nvs_get_str(nvs_handle, USERNAME_KEY, saved_username, &username_size);
    if (err != ESP_OK) {
        printf("Read failed!\n");
        nvs_close(nvs_handle);
        return false;
    }

    nvs_close(nvs_handle);

    return err;
}

// Generate a random salt value
void generate_salt(uint8_t *salt, size_t length) {
    for (size_t i = 0; i < length; i++) {
        salt[i] = esp_random() % 256; // Generate random numbers
    }
}

// Compute the password hash using SHA-256
void compute_hash(const char *password, const uint8_t *salt, size_t salt_len, uint8_t *output_hash) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0); // 0 means SHA-256

    // Input password and salt value
    mbedtls_sha256_update(&ctx, (const uint8_t *)password, strlen(password));
    mbedtls_sha256_update(&ctx, salt, salt_len);

    // Compute the hash
    mbedtls_sha256_finish(&ctx, output_hash);
    mbedtls_sha256_free(&ctx);
}

// Save the username, salt, and password hash to NVS
esp_err_t save_credentials(const char *username, const char *password) {
    uint8_t salt[SALT_LENGTH];
    uint8_t password_hash[HASH_LENGTH];

    // Generate salt and compute password hash
    generate_salt(salt, SALT_LENGTH);
    compute_hash(password, salt, SALT_LENGTH, password_hash);

    // Open NVS for read-write access
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        printf("Error opening NVS handle!\n");
        return err;
    }

    // Save username to NVS
    err = nvs_set_str(nvs_handle, USERNAME_KEY, username);
    if (err != ESP_OK) {
        printf("Failed to save username!\n");
        nvs_close(nvs_handle);
        return err;
    }

    // Save salt to NVS
    err = nvs_set_blob(nvs_handle, SALT_KEY, salt, SALT_LENGTH);
    if (err != ESP_OK) {
        printf("Failed to save salt!\n");
        nvs_close(nvs_handle);
        return err;
    }

    // Save password hash to NVS
    err = nvs_set_blob(nvs_handle, PASSWORD_HASH_KEY, password_hash, HASH_LENGTH);
    if (err != ESP_OK) {
        printf("Failed to save password hash!\n");
        nvs_close(nvs_handle);
        return err;
    }

    // Commit changes to NVS
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    return err;
}

// Verify the username and password
bool verify_credentials(const char *input_username, const char *input_password) {
    uint8_t salt[SALT_LENGTH];
    uint8_t saved_hash[HASH_LENGTH];
    uint8_t computed_hash[HASH_LENGTH];
    char saved_username[MAX_LENGTH];

    // Open NVS for reading
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        printf("Error opening NVS handle!\n");
        return false;
    }

    // Read username from NVS
    size_t username_size = sizeof(saved_username);
    err = nvs_get_str(nvs_handle, USERNAME_KEY, saved_username, &username_size);
    if (err != ESP_OK || strcmp(input_username, saved_username) != 0) {
        printf("Incorrect username!\n");
        nvs_close(nvs_handle);
        return false;
    }

    // Read salt from NVS
    size_t salt_size = SALT_LENGTH;
    err = nvs_get_blob(nvs_handle, SALT_KEY, salt, &salt_size);
    if (err != ESP_OK) {
        printf("Failed to read salt!\n");
        nvs_close(nvs_handle);
        return false;
    }

    // Read password hash from NVS
    size_t hash_size = HASH_LENGTH;
    err = nvs_get_blob(nvs_handle, PASSWORD_HASH_KEY, saved_hash, &hash_size);
    if (err != ESP_OK) {
        printf("Failed to read password hash!\n");
        nvs_close(nvs_handle);
        return false;
    }

    nvs_close(nvs_handle);

    // Compute hash for the input password
    compute_hash(input_password, salt, SALT_LENGTH, computed_hash);

    // Verify the hash values match
    return memcmp(computed_hash, saved_hash, HASH_LENGTH) == 0;
}
