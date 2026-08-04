#pragma once

#include <stdbool.h>
#include <stddef.h>

#define STATION_SETTINGS_PATH "/littlefs/config/settings.csv"
#define STATION_SETTINGS_LAST_URL_KEY "last_station_url"

bool station_resume_save_last_url(const char *path, const char *url);
bool station_resume_load_last_url(const char *path, char *url, size_t url_size);
