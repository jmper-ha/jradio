#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "settings_csv.h"

int main(void)
{
    const char *path = "/tmp/jradio_settings_csv_test.csv";
    char value[64] = {0};
    FILE *file = fopen(path, "w");
    assert(file != NULL);
    assert(fputs("volume,80\nlast_station_url,http://old\nlast_station_url,http://duplicate\n", file) >= 0);
    assert(fclose(file) == 0);
    assert(settings_csv_set(path, "last_station_url", "http://new"));
    assert(settings_csv_get(path, "last_station_url", value, sizeof(value)));
    assert(strcmp(value, "http://new") == 0);
    assert(settings_csv_get(path, "volume", value, sizeof(value)));
    assert(strcmp(value, "80") == 0);
    assert(settings_csv_set(path, "brightness", "70"));
    assert(settings_csv_get(path, "brightness", value, sizeof(value)));
    assert(strcmp(value, "70") == 0);
    assert(!settings_csv_set(path, "bad,key", "value"));
    assert(!settings_csv_set(path, "bad", "line\nfeed"));
    assert(remove(path) == 0);
    puts("settings_csv tests passed");
    return 0;
}
