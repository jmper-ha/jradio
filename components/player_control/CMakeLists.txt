idf_component_register(
    SRCS "player_control_logic.c" "player_control.c"
    INCLUDE_DIRS "include"
    REQUIRES audio internet_radio jradio_wifi_provisioning
)
