#include "remote_control.h"

#include "board_features.h"

#if BOARD_HAS_IR

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "board_input.h"
#include "ir_receiver.h"
#include "ir_wake_stub.h"

static const char *TAG = "remote";

#define REMOTE_MAP_TEMP_PATH REMOTE_MAP_PATH ".tmp"
#define REMOTE_MAP_TEXT_MAX 2048U

/* A repeat frame belongs to the key before it only while the remote is still
 * sending them: NEC repeats every 108 ms, so a gap wider than this is a new
 * press of the same key, not the old one held. */
#define REMOTE_REPEAT_GAP_MS 250U
/* A held key starts ramping after this long, so a press that is a little slow
 * to come off the key does not step the volume twice. */
#define REMOTE_HOLD_BEFORE_RAMP_MS 350U
/* And ramps a step per this many repeat frames: every frame was measured as
 * the whole volume range in two seconds, which is a jump, not a ramp. Every
 * second frame is about five steps a second - four seconds end to end, the
 * pace a television's remote has. */
#define REMOTE_RAMP_EVERY 2U
/* Remotes that repeat by resending the whole frame send it about this often;
 * the same key seen sooner than this is a hold, not two presses. */
#define REMOTE_RAW_REPEAT_MS 150U

static remote_map_t s_map;
static remote_function_t s_learning = REMOTE_FUNCTION_COUNT;
static uint32_t s_learning_armed_ms;
static uint32_t s_revision;
static SemaphoreHandle_t s_lock;

/* What the last real frame was, for the repeat frames that follow it. */
static remote_function_t s_held = REMOTE_FUNCTION_COUNT;
static ir_code_t s_held_code;
static uint32_t s_held_since_ms;
static uint32_t s_held_last_ms;
static uint32_t s_held_repeats;

/* For the page: whatever came last, learned or not, repeats included. */
static remote_last_key_t s_last;
static uint32_t s_last_ms;

static bool s_loaded;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool map_save_locked(void)
{
    static char text[REMOTE_MAP_TEXT_MAX];
    remote_map_format(&s_map, text, sizeof(text));
    FILE *file = fopen(REMOTE_MAP_TEMP_PATH, "w");
    if (file == NULL) {
        ESP_LOGE(TAG, "cannot write %s", REMOTE_MAP_TEMP_PATH);
        return false;
    }
    const size_t length = strlen(text);
    const bool written = fwrite(text, 1, length, file) == length;
    fclose(file);
    if (!written) {
        remove(REMOTE_MAP_TEMP_PATH);
        return false;
    }
    /* Into place in one step, like every other file on the partition: a power
     * cut leaves either the old table or the new one, never half. LittleFS
     * renames over the old file itself; a remove() before it left a window,
     * and a crash in that window on the bench cost the whole table. */
    if (rename(REMOTE_MAP_TEMP_PATH, REMOTE_MAP_PATH) != 0) {
        ESP_LOGE(TAG, "cannot put %s in place", REMOTE_MAP_PATH);
        return false;
    }
    return true;
}

static void map_load(void)
{
    static char text[REMOTE_MAP_TEXT_MAX];
    FILE *file = fopen(REMOTE_MAP_PATH, "r");
    if (file == NULL) {
        remote_map_init(&s_map);
        ESP_LOGI(TAG, "no %s; the kit remote's table", REMOTE_MAP_PATH);
        return;
    }
    const size_t length = fread(text, 1, sizeof(text) - 1U, file);
    fclose(file);
    text[length] = '\0';
    const size_t skipped = remote_map_parse(&s_map, text);
    unsigned bound = 0U;
    for (unsigned index = 0U; index < (unsigned)REMOTE_FUNCTION_COUNT; ++index) {
        if (s_map.bound[index]) ++bound;
    }
    ESP_LOGI(TAG, "%s: %u keys bound%s", REMOTE_MAP_PATH, bound, skipped ? ", some lines skipped" : "");
    if (skipped) ESP_LOGW(TAG, "%u lines of %s could not be read", (unsigned)skipped, REMOTE_MAP_PATH);
}

static void act(remote_function_t function)
{
    const board_input_action_t action = remote_function_action(function);
    if (!board_input_inject(action)) {
        ESP_LOGW(TAG, "%s: the input queue is full", remote_function_name(function));
    }
}

/* Runs on the receiver's task. */
static void on_key(const ir_code_t *code, void *context)
{
    (void)context;
    const uint32_t now = now_ms();
    xSemaphoreTake(s_lock, portMAX_DELAY);

    if (code->kind == IR_CODE_NEC_REPEAT) {
        /* A repeat only keeps the last key fresh. */
        if (s_last.seen) s_last_ms = now;
    } else {
        s_last.function = remote_map_lookup(&s_map, code);
        s_last.code = *code;
        s_last.seen = true;
        s_last_ms = now;
    }

    /* Learning first: an armed function takes the next real key and nothing
     * acts, whatever the key used to mean. */
    if (s_learning != REMOTE_FUNCTION_COUNT) {
        if ((uint32_t)(now - s_learning_armed_ms) >= REMOTE_LEARN_MS) {
            ESP_LOGI(TAG, "learning %s: no key in %u s, disarmed",
                     remote_function_name(s_learning), REMOTE_LEARN_MS / 1000U);
            s_learning = REMOTE_FUNCTION_COUNT;
            ++s_revision;
        } else if (code->kind == IR_CODE_NEC || code->kind == IR_CODE_RAW) {
            char text[24];
            ir_code_format(code, text, sizeof(text));
            const remote_function_t displaced = remote_map_learn(&s_map, s_learning, code);
            ESP_LOGI(TAG, "learned %s = %s%s%s", remote_function_name(s_learning), text,
                     displaced != REMOTE_FUNCTION_COUNT ? ", taken from " : "",
                     displaced != REMOTE_FUNCTION_COUNT ? remote_function_name(displaced) : "");
            s_learning = REMOTE_FUNCTION_COUNT;
            (void)map_save_locked();
            ++s_revision;
            s_held = REMOTE_FUNCTION_COUNT;
            xSemaphoreGive(s_lock);
            return;
        } else {
            xSemaphoreGive(s_lock);
            return;
        }
    }

    remote_function_t function = REMOTE_FUNCTION_COUNT;
    bool repeat = false;
    if (code->kind == IR_CODE_NEC_REPEAT) {
        /* A repeat with no key before it, or one too long ago, is noise. */
        if (s_held != REMOTE_FUNCTION_COUNT && (uint32_t)(now - s_held_last_ms) <= REMOTE_REPEAT_GAP_MS) {
            function = s_held;
            repeat = true;
        }
    } else {
        function = remote_map_lookup(&s_map, code);
        if (function == REMOTE_FUNCTION_COUNT) {
            char text[24];
            ir_code_format(code, text, sizeof(text));
            ESP_LOGD(TAG, "%s is not a key of ours", text);
            s_held = REMOTE_FUNCTION_COUNT;
            xSemaphoreGive(s_lock);
            return;
        }
        /* The same key again within a repeat's time is the key held, on a
         * remote that repeats by sending the frame again. */
        if (s_held == function && ir_code_same_key(&s_held_code, code) &&
            (uint32_t)(now - s_held_last_ms) <= REMOTE_RAW_REPEAT_MS) {
            repeat = true;
        } else {
            s_held = function;
            s_held_code = *code;
            s_held_since_ms = now;
            s_held_repeats = 0U;
        }
    }
    s_held_last_ms = now;
    if (repeat) ++s_held_repeats;
    const uint32_t repeats = s_held_repeats;
    xSemaphoreGive(s_lock);

    if (function == REMOTE_FUNCTION_COUNT) return;
    if (repeat) {
        if (!remote_function_repeats(function)) return;
        if ((uint32_t)(now - s_held_since_ms) < REMOTE_HOLD_BEFORE_RAMP_MS) return;
        if (repeats % REMOTE_RAMP_EVERY != 0U) return;
    }
    act(function);
}

static esp_err_t load_once(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) return ESP_ERR_NO_MEM;
    }
    if (!s_loaded) {
        map_load();
        s_loaded = true;
    }
    return ESP_OK;
}

esp_err_t remote_control_init(void)
{
    const esp_err_t result = load_once();
    if (result != ESP_OK) return result;
    ir_receiver_set_listener(on_key, NULL);
    return ESP_OK;
}

/* The wake listener keeps the keys it hears rather than judging them: it
 * starts before the table is read. A few are enough - the check drains them
 * every hundred milliseconds - and the time the line was last busy is kept
 * too, because a held key stretches the wait and the board should not go
 * back down under a remote that is still talking. */
#define REMOTE_WAKE_KEEP 4U
static SemaphoreHandle_t s_wake_seen;
static portMUX_TYPE s_wake_mux = portMUX_INITIALIZER_UNLOCKED;
static ir_code_t s_wake_codes[REMOTE_WAKE_KEEP];
static unsigned s_wake_count;
static uint32_t s_wake_busy_ms;
static uint32_t s_wake_started_ms;
static bool s_wake_listening;

static void on_wake_key(const ir_code_t *code, void *context)
{
    (void)context;
    portENTER_CRITICAL(&s_wake_mux);
    s_wake_busy_ms = now_ms();
    if ((code->kind == IR_CODE_NEC || code->kind == IR_CODE_RAW) && s_wake_count < REMOTE_WAKE_KEEP) {
        s_wake_codes[s_wake_count++] = *code;
    }
    portEXIT_CRITICAL(&s_wake_mux);
    if (s_wake_seen != NULL) xSemaphoreGive(s_wake_seen);
}

void remote_control_wake_listen(void)
{
    s_wake_seen = xSemaphoreCreateBinary();
    if (s_wake_seen == NULL) return;
    s_wake_started_ms = now_ms();
    s_wake_busy_ms = s_wake_started_ms;
    ir_receiver_set_listener(on_wake_key, NULL);
    if (ir_receiver_init() != ESP_OK) return;
    s_wake_listening = true;
}

/* Whether any key kept so far is Power; the kept keys are consumed. */
static bool wake_keys_had_power(void)
{
    ir_code_t codes[REMOTE_WAKE_KEEP];
    portENTER_CRITICAL(&s_wake_mux);
    const unsigned count = s_wake_count;
    memcpy(codes, s_wake_codes, sizeof(codes));
    s_wake_count = 0U;
    portEXIT_CRITICAL(&s_wake_mux);
    bool power = false;
    for (unsigned index = 0U; index < count; ++index) {
        char text[24];
        ir_code_format(&codes[index], text, sizeof(text));
        const bool is_power = remote_map_lookup(&s_map, &codes[index]) == REMOTE_POWER;
        ESP_LOGI(TAG, "wake: %s is %sPower", text, is_power ? "" : "not ");
        power = power || is_power;
    }
    return power;
}

bool remote_control_wake_check(void)
{
    if (!s_wake_listening) remote_control_wake_listen();
    if (!s_wake_listening || load_once() != ESP_OK) return false;
    /* The frame that woke the chip, if the stub caught it: that is the first
     * press answered, with no second one to wait for. */
    ir_code_t woke_with;
    bool power = false;
    if (ir_wake_stub_take(&woke_with)) {
        char text[24];
        ir_code_format(&woke_with, text, sizeof(text));
        power = remote_map_lookup(&s_map, &woke_with) == REMOTE_POWER;
        ESP_LOGI(TAG, "wake: woke on %s, %sPower", text, power ? "" : "not ");
    }
    power = power || wake_keys_had_power();
    while (!power) {
        const uint32_t now = now_ms();
        const uint32_t since_start = now - s_wake_started_ms;
        portENTER_CRITICAL(&s_wake_mux);
        const uint32_t since_busy = now - s_wake_busy_ms;
        portEXIT_CRITICAL(&s_wake_mux);
        /* Out of time, and the remote has been quiet for a moment. */
        if (since_start >= REMOTE_WAKE_LISTEN_MS && since_busy >= REMOTE_REPEAT_GAP_MS) break;
        (void)xSemaphoreTake(s_wake_seen, pdMS_TO_TICKS(100));
        power = wake_keys_had_power();
    }
    ir_receiver_set_listener(NULL, NULL);
    s_wake_listening = false;
    return power;
}

bool remote_control_learn(remote_function_t function)
{
    if ((unsigned)function >= (unsigned)REMOTE_FUNCTION_COUNT || s_lock == NULL) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_learning = function;
    s_learning_armed_ms = now_ms();
    ++s_revision;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "learning %s: press the key", remote_function_name(function));
    return true;
}

bool remote_control_forget(remote_function_t function)
{
    if ((unsigned)function >= (unsigned)REMOTE_FUNCTION_COUNT || s_lock == NULL) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    remote_map_forget(&s_map, function);
    if (s_learning == function) s_learning = REMOTE_FUNCTION_COUNT;
    const bool saved = map_save_locked();
    ++s_revision;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "forgot %s", remote_function_name(function));
    return saved;
}

void remote_control_snapshot(remote_map_t *map, remote_function_t *learning)
{
    if (s_lock == NULL) {
        if (map != NULL) remote_map_init(map);
        if (learning != NULL) *learning = REMOTE_FUNCTION_COUNT;
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    /* An arming that ran out is reported as over, and cleared, so the page
     * does not show "press a key" for ever. */
    if (s_learning != REMOTE_FUNCTION_COUNT &&
        (uint32_t)(now_ms() - s_learning_armed_ms) >= REMOTE_LEARN_MS) {
        s_learning = REMOTE_FUNCTION_COUNT;
        ++s_revision;
    }
    if (map != NULL) *map = s_map;
    if (learning != NULL) *learning = s_learning;
    xSemaphoreGive(s_lock);
}

uint32_t remote_control_revision(void)
{
    return s_revision;
}

void remote_control_last_key(remote_last_key_t *last)
{
    if (last == NULL) return;
    if (s_lock == NULL) {
        memset(last, 0, sizeof(*last));
        last->function = REMOTE_FUNCTION_COUNT;
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *last = s_last;
    last->age_ms = s_last.seen ? (uint32_t)(now_ms() - s_last_ms) : 0U;
    xSemaphoreGive(s_lock);
}

#else

esp_err_t remote_control_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

bool remote_control_learn(remote_function_t function)
{
    (void)function;
    return false;
}

bool remote_control_forget(remote_function_t function)
{
    (void)function;
    return false;
}

void remote_control_snapshot(remote_map_t *map, remote_function_t *learning)
{
    if (map != NULL) remote_map_clear(map);
    if (learning != NULL) *learning = REMOTE_FUNCTION_COUNT;
}

uint32_t remote_control_revision(void)
{
    return 0U;
}

void remote_control_wake_listen(void)
{
}

bool remote_control_wake_check(void)
{
    return false;
}

void remote_control_last_key(remote_last_key_t *last)
{
    if (last == NULL) return;
    *last = (remote_last_key_t){.function = REMOTE_FUNCTION_COUNT};
}

#endif
