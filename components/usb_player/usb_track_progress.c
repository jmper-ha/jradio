#include "usb_track_progress.h"

#include <stdio.h>

uint32_t usb_track_elapsed_seconds(uint64_t pcm_bytes, uint32_t sample_rate_hz,
                                   uint8_t channels, uint8_t bits_per_sample)
{
    if (sample_rate_hz == 0U || channels == 0U || bits_per_sample < 8U) return 0U;
    const uint64_t bytes_per_second =
        (uint64_t)sample_rate_hz * channels * (bits_per_sample / 8U);
    if (bytes_per_second == 0U) return 0U;
    return (uint32_t)(pcm_bytes / bytes_per_second);
}

uint32_t usb_track_total_seconds(uint64_t file_bytes, uint64_t header_bytes,
                                 uint16_t bitrate_kbps)
{
    if (bitrate_kbps == 0U) return 0U;
    if (file_bytes <= header_bytes) return 0U;
    const uint64_t audio_bytes = file_bytes - header_bytes;
    /* bits / bits-per-second. Done in this order so a short file does not
     * truncate to zero before the division by the rate. */
    return (uint32_t)((audio_bytes * 8U) / ((uint64_t)bitrate_kbps * 1000U));
}

uint64_t usb_track_seek_offset(uint64_t file_bytes, uint64_t header_bytes,
                               uint16_t bitrate_kbps, uint32_t target_seconds)
{
    if (bitrate_kbps == 0U || file_bytes <= header_bytes) return header_bytes;
    const uint64_t audio_bytes = file_bytes - header_bytes;
    const uint64_t wanted = ((uint64_t)target_seconds * bitrate_kbps * 1000U) / 8U;
    return header_bytes + (wanted < audio_bytes ? wanted : audio_bytes);
}

uint64_t usb_track_pcm_bytes(uint32_t seconds, uint32_t sample_rate_hz,
                             uint8_t channels, uint8_t bits_per_sample)
{
    if (sample_rate_hz == 0U || channels == 0U || bits_per_sample < 8U) return 0U;
    return (uint64_t)seconds * sample_rate_hz * channels * (bits_per_sample / 8U);
}

bool usb_track_progress_percent(uint32_t elapsed_s, uint32_t total_s, uint8_t *percent)
{
    if (percent == NULL || total_s == 0U) return false;
    if (elapsed_s >= total_s) {
        *percent = 100U;
        return true;
    }
    *percent = (uint8_t)(((uint64_t)elapsed_s * 100U) / total_s);
    return true;
}

void usb_track_time_text(char *text, size_t text_size, uint32_t seconds)
{
    if (text == NULL || text_size == 0U) return;
    const uint32_t hours = seconds / 3600U;
    const uint32_t minutes = (seconds / 60U) % 60U;
    const uint32_t remainder = seconds % 60U;
    if (hours > 0U) {
        snprintf(text, text_size, "%u:%02u:%02u", (unsigned int)hours, (unsigned int)minutes,
                 (unsigned int)remainder);
    } else {
        snprintf(text, text_size, "%u:%02u", (unsigned int)minutes, (unsigned int)remainder);
    }
}
