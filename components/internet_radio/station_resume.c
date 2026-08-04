#include "station_resume.h"

#include <stdio.h>
#include <string.h>

#include "settings_csv.h"
#include "station_catalog.h"

static bool valid_url(const char *url)
{
    return url != NULL && url[0] != '\0' && strchr(url, ',') == NULL &&
           strpbrk(url, "\r\n") == NULL;
}

bool station_resume_save_last_url(const char *path, const char *url)
{
    return valid_url(url) && settings_csv_set(path, STATION_SETTINGS_LAST_URL_KEY, url);
}

bool station_resume_load_last_url(const char *path, char *url, size_t url_size)
{
    if (path == NULL || url == NULL || url_size < 2) return false;
    if (settings_csv_get(path, STATION_SETTINGS_LAST_URL_KEY, url, url_size)) return true;

    /* Accept the previous one-line format once, easing migration to settings.csv. */
    FILE *file = fopen(path, "r");
    if (file == NULL) return false;
    char line[STATION_CATALOG_URL_MAX_LEN];
    while (fgets(line, sizeof(line), file) != NULL) {
        if (strstr(line, "://") == NULL) continue;
        line[strcspn(line, "\r\n")] = '\0';
        const size_t length = strlen(line);
        if (length == 0 || length >= url_size || !valid_url(line)) {
            fclose(file);
            return false;
        }
        memcpy(url, line, length + 1);
        fclose(file);
        return true;
    }
    fclose(file);
    return false;
}
