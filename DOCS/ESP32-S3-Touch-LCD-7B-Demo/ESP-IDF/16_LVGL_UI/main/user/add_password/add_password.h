#ifndef _ADD_PASSWORD_
#define _ADD_PASSWORD_
#include "esp_err.h"

// Define the length of the password hash and salt value
#define HASH_LENGTH 32       // Length of the SHA-256 hash output (256 bits)
#define SALT_LENGTH 16       // Length of the salt (128 bits)

// Define the NVS (Non-Volatile Storage) namespace and keys
#define NVS_NAMESPACE "storage"        // Namespace for storing credentials in NVS
#define USERNAME_KEY "username"        // Key for storing the username
#define PASSWORD_HASH_KEY "password_hash"  // Key for storing the password hash
#define SALT_KEY "salt"                // Key for storing the salt value

// Function to initialize NVS (Non-Volatile Storage)
esp_err_t init_nvs();

// Function to save the username and hashed password (with salt) into NVS
esp_err_t save_credentials(const char *username, const char *password);

// Function to verify if the input username and password are correct by comparing the hashes
bool verify_credentials(const char *input_username, const char *input_password);

#endif
