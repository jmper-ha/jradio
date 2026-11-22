/* Dual encoder example: copy to the repository root as board_options.local.h. */
#include "examples/max98357a/max98357a_audio_profile.h"

#undef CONTROLS
#define CONTROLS CONTROLS_DUAL_ENCODER
#undef ENCODER_LEFT_GPIO
#define ENCODER_LEFT_GPIO 7
#undef ENCODER_RIGHT_GPIO
#define ENCODER_RIGHT_GPIO 5
#undef ENCODER_BUTTON_GPIO
#define ENCODER_BUTTON_GPIO 6
#undef ENCODER_AS_BUTTONS
#define ENCODER_AS_BUTTONS 0
#define VOLUME_ENCODER_A_GPIO 13
#define VOLUME_ENCODER_B_GPIO 14
#define ENCODER_EDGES_PER_DETENT 4
#define VOLUME_ENCODER_EDGES_PER_DETENT 4
#undef BUTTON_QUICK_MENU_GPIO
#define BUTTON_MENU_GPIO 21
