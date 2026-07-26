#include "ui.h"
#include "sd_card.h"

static char output[1024]; // Buffer to store the formatted SD card information

/**
 * @brief Callback function for the LVGL timer to update the UI with SD card information.
 * 
 * This function is called periodically by the LVGL timer to update the UI label with the formatted SD card information.
 * 
 * @param timer Pointer to the LVGL timer object
 */
static void micro_sd_card_cb(lv_timer_t * timer) 
{
    lv_label_set_text(ui_PWM_Label6, output); // Set the text of the UI label to the formatted SD card information
}

/**
 * @brief Initialize the SD card and retrieve its information.
 * 
 * This function initializes the SD card, retrieves its information (such as name, type, speed, size, etc.), 
 * formats the information into a string, and updates the UI label with the formatted string.
 * 
 * @return None
 */
void sd_init()
{
    esp_err_t ret = sd_mmc_init(); // Initialize the SD card
    static char* p = output; // Pointer to the output buffer

    if (ret == ESP_OK)
    {
        // Name
        p += sprintf(p, "Name: %s\n", card->cid.name); // Get the card's name from the CID register

        // Type
        const char* type;
        if (card->is_sdio) {
            type = "SDIO"; // SDIO card
        } else if (card->is_mmc) {
            type = "MMC"; // MultiMediaCard
        } else {
            type = (card->ocr & 0x40000000) ? "SDHC/SDXC" : "SDSC"; // Determine if the card is SDHC/SDXC or SDSC
        }
        p += sprintf(p, "Type: %s\n", type); // Format the card type

        // Speed
        if (card->real_freq_khz == 0) {
            p += sprintf(p, "Speed: N/A\n"); // If the frequency is not available
        } else {
            const char* freq_unit = card->real_freq_khz < 1000 ? "kHz" : "MHz"; // Determine the frequency unit
            const float freq = card->real_freq_khz < 1000 ? card->real_freq_khz : card->real_freq_khz / 1000.0; // Convert frequency to appropriate unit
            const char* max_freq_unit = card->max_freq_khz < 1000 ? "kHz" : "MHz"; // Determine the maximum frequency unit
            const float max_freq = card->max_freq_khz < 1000 ? card->max_freq_khz : card->max_freq_khz / 1000.0; // Convert maximum frequency to appropriate unit
            p += sprintf(p, "Speed: %.2f %s (limit: %.2f %s)%s\n", freq, freq_unit, max_freq, max_freq_unit, card->is_ddr ? ", DDR" : ""); // Format the speed information
        }

        // Size
        uint64_t size_mb = ((uint64_t)card->csd.capacity) * card->csd.sector_size / (1024 * 1024); // Calculate the card size in MB
        p += sprintf(p, "Size: %lluMB\n", size_mb); // Format the card size

        // CSD (Card-Specific Data)
        p += sprintf(p, "CSD: ver=%d, sector_size=%d, capacity=%d read_bl_len=%d\n",
                    (int)(card->is_mmc ? card->csd.csd_ver : card->csd.csd_ver + 1), // CSD version
                    card->csd.sector_size, // Sector size
                    card->csd.capacity, // Capacity
                    card->csd.read_block_len); // Read block length

        // EXT CSD (Extended Card-Specific Data)
        if (card->is_mmc) {
            p += sprintf(p, "EXT CSD: bus_width=%u\n", (unsigned)(1 << card->log_bus_width)); // Format the bus width from EXT CSD
        }

        // SSR (Speed Class Specification Register)
        if (!card->is_sdio) {
            p += sprintf(p, "SSR: bus_width=%u\n", (unsigned)(card->ssr.cur_bus_width ? 4 : 1)); // Format the bus width from SSR
        }

        // SCR (SD Configuration Register)
        if (card->is_sdio) {
            p += sprintf(p, "SCR: sd_spec=%d, bus_width=%d\n", card->scr.sd_spec, card->scr.bus_width); // Format the SD specification and bus width from SCR
        }
        
        sd_mmc_unmount(); // Unmount the SD card

        lv_timer_t *t = lv_timer_create(micro_sd_card_cb, 100, NULL);  // Create an LVGL timer to update the UI every 100ms
        lv_timer_set_repeat_count(t, 1); // Set the timer to repeat only once

    }
    else{
        p += sprintf(p, "Memory card mounting failed.\n"); // If the SD card mounting fails
        lv_timer_t *t = lv_timer_create(micro_sd_card_cb, 100, NULL);  // Create an LVGL timer to update the UI every 100ms
        lv_timer_set_repeat_count(t, 1); // Set the timer to repeat only once
    }

}