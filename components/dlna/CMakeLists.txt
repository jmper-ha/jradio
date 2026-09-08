# Split the way the rest of the firmware is split: the first line of sources is
# text handling with no ESP-IDF in it, compiled unchanged by the host tests, and
# the second is the device shell - a UDP socket and two HTTP requests - which is
# only ever built here.
idf_component_register(SRCS "dlna_xml.c" "dlna_url.c" "dlna_ssdp.c" "dlna_device.c"
                            "dlna_soap.c" "dlna_didl.c" "dlna_browse_stack.c"
                            "dlna_root_filter.c"
                            "dlna_client.c" "dlna_discovery.c" "dlna_source.c"
                       INCLUDE_DIRS "include"
                       # radio_stream_format.h: a server hands out an HTTP URL
                       # with a MIME type, and which decoder that means is a
                       # question internet_radio already answers.
                       REQUIRES internet_radio audio_tags
                       # settings: a resume point is written to settings.csv,
                       # which sizes its own fields - the two are checked
                       # against each other in dlna_source.c.
                       PRIV_REQUIRES esp_http_client esp_timer lwip settings)
