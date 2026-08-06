#include <rp6502.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "constants.h"
#include "input.h"

#define JOYSTICK_CONFIG_FILE "JOYSTICK_SH.DAT"

enum {
    KEY_B = 0x05,
    KEY_H = 0x0B,
    KEY_P = 0x13,
    KEY_X = 0x1B,
    KEY_Z = 0x1D,
    KEY_ENTER = 0x28,
    KEY_ESC = 0x29,
    KEY_SPACE = 0x2C,
    KEY_COMMA = 0x36,
    KEY_PERIOD = 0x37,
    KEY_SLASH = 0x38,
    KEY_RIGHT = 0x4F,
    KEY_LEFT = 0x50,
    KEY_DOWN = 0x51,
    KEY_UP = 0x52
};

enum {
    GP_DPAD_UP = 0x01,
    GP_DPAD_DOWN = 0x02,
    GP_DPAD_LEFT = 0x04,
    GP_DPAD_RIGHT = 0x08,
    GP_CONNECTED = 0x80,
    GP_LSTICK_UP = 0x01,
    GP_LSTICK_DOWN = 0x02,
    GP_LSTICK_LEFT = 0x04,
    GP_LSTICK_RIGHT = 0x08,
    GP_BTN_A = 0x01,
    GP_BTN_B = 0x02,
    GP_BTN_X = 0x08,
    GP_BTN_Y = 0x10,
    GP_BTN_SELECT = 0x04,
    GP_BTN_START = 0x08
};

enum {
    GP_FIELD_DPAD = 0,
    GP_FIELD_STICKS = 1,
    GP_FIELD_BTN0 = 2,
    GP_FIELD_BTN1 = 3
};

enum {
    ACTION_MOVE_UP = 0,
    ACTION_MOVE_DOWN,
    ACTION_MOVE_LEFT,
    ACTION_MOVE_RIGHT,
    ACTION_BTN_A,
    ACTION_BTN_B,
    ACTION_BTN_X,
    ACTION_BTN_Y,
    ACTION_BTN_LT,
    ACTION_BTN_RT,
    ACTION_BTN_SELECT,
    ACTION_BTN_START,
    ACTION_COUNT
};

typedef struct gamepad_state_s {
    uint8_t dpad;
    uint8_t sticks;
    uint8_t btn0;
    uint8_t btn1;
    int8_t lx;
    int8_t ly;
    int8_t rx;
    int8_t ry;
    uint8_t l2;
    uint8_t r2;
} gamepad_state_t;

typedef struct button_mapping_s {
    uint8_t keyboard_key;
    uint8_t gamepad_button;
    uint8_t gamepad_mask;
    uint8_t gamepad_button2;
    uint8_t gamepad_mask2;
} button_mapping_t;

typedef struct joystick_mapping_s {
    uint8_t action_id;
    uint8_t field;
    uint8_t mask;
} joystick_mapping_t;

static uint8_t s_keystate[32];
static gamepad_state_t s_gamepad0;
static button_mapping_t s_button_mappings[ACTION_COUNT];
static input_actions_t s_last_actions;

static bool key_is_down(uint8_t hid_key)
{
    return (s_keystate[hid_key >> 3] & (1u << (hid_key & 7))) != 0u;
}

static bool gamepad_connected(void)
{
    return (s_gamepad0.dpad & GP_CONNECTED) != 0u;
}

static uint8_t gamepad_field_value(uint8_t field)
{
    switch (field) {
        case GP_FIELD_DPAD:
            return s_gamepad0.dpad;
        case GP_FIELD_STICKS:
            return s_gamepad0.sticks;
        case GP_FIELD_BTN0:
            return s_gamepad0.btn0;
        case GP_FIELD_BTN1:
            return s_gamepad0.btn1;
        default:
            return 0u;
    }
}

static void reset_default_mappings(void)
{
    memset(s_button_mappings, 0, sizeof(s_button_mappings));

    s_button_mappings[ACTION_MOVE_UP].keyboard_key = KEY_UP;
    s_button_mappings[ACTION_MOVE_UP].gamepad_button = GP_FIELD_STICKS;
    s_button_mappings[ACTION_MOVE_UP].gamepad_mask = GP_LSTICK_UP;
    s_button_mappings[ACTION_MOVE_UP].gamepad_button2 = GP_FIELD_DPAD;
    s_button_mappings[ACTION_MOVE_UP].gamepad_mask2 = GP_DPAD_UP;

    s_button_mappings[ACTION_MOVE_DOWN].keyboard_key = KEY_DOWN;
    s_button_mappings[ACTION_MOVE_DOWN].gamepad_button = GP_FIELD_STICKS;
    s_button_mappings[ACTION_MOVE_DOWN].gamepad_mask = GP_LSTICK_DOWN;
    s_button_mappings[ACTION_MOVE_DOWN].gamepad_button2 = GP_FIELD_DPAD;
    s_button_mappings[ACTION_MOVE_DOWN].gamepad_mask2 = GP_DPAD_DOWN;

    s_button_mappings[ACTION_MOVE_LEFT].keyboard_key = KEY_LEFT;
    s_button_mappings[ACTION_MOVE_LEFT].gamepad_button = GP_FIELD_STICKS;
    s_button_mappings[ACTION_MOVE_LEFT].gamepad_mask = GP_LSTICK_LEFT;
    s_button_mappings[ACTION_MOVE_LEFT].gamepad_button2 = GP_FIELD_DPAD;
    s_button_mappings[ACTION_MOVE_LEFT].gamepad_mask2 = GP_DPAD_LEFT;

    s_button_mappings[ACTION_MOVE_RIGHT].keyboard_key = KEY_RIGHT;
    s_button_mappings[ACTION_MOVE_RIGHT].gamepad_button = GP_FIELD_STICKS;
    s_button_mappings[ACTION_MOVE_RIGHT].gamepad_mask = GP_LSTICK_RIGHT;
    s_button_mappings[ACTION_MOVE_RIGHT].gamepad_button2 = GP_FIELD_DPAD;
    s_button_mappings[ACTION_MOVE_RIGHT].gamepad_mask2 = GP_DPAD_RIGHT;

    s_button_mappings[ACTION_BTN_Y].keyboard_key = KEY_PERIOD;
    s_button_mappings[ACTION_BTN_Y].gamepad_button = GP_FIELD_BTN0;
    s_button_mappings[ACTION_BTN_Y].gamepad_mask = GP_BTN_Y;

    s_button_mappings[ACTION_BTN_A].keyboard_key = KEY_X;
    s_button_mappings[ACTION_BTN_A].gamepad_button = GP_FIELD_BTN0;
    s_button_mappings[ACTION_BTN_A].gamepad_mask = GP_BTN_A;

    s_button_mappings[ACTION_BTN_X].keyboard_key = KEY_Z;
    s_button_mappings[ACTION_BTN_X].gamepad_button = GP_FIELD_BTN0;
    s_button_mappings[ACTION_BTN_X].gamepad_mask = GP_BTN_X;

    s_button_mappings[ACTION_BTN_B].keyboard_key = 0u;
    s_button_mappings[ACTION_BTN_B].gamepad_button = GP_FIELD_BTN0;
    s_button_mappings[ACTION_BTN_B].gamepad_mask = GP_BTN_B;

    s_button_mappings[ACTION_BTN_SELECT].keyboard_key = KEY_H;
    s_button_mappings[ACTION_BTN_SELECT].gamepad_button = GP_FIELD_BTN1;
    s_button_mappings[ACTION_BTN_SELECT].gamepad_mask = GP_BTN_SELECT;

    s_button_mappings[ACTION_BTN_START].keyboard_key = KEY_P;
    s_button_mappings[ACTION_BTN_START].gamepad_button = GP_FIELD_BTN1;
    s_button_mappings[ACTION_BTN_START].gamepad_mask = GP_BTN_START;
}

static bool load_button_mappings(void)
{
    FILE *fp;
    int count;

    fp = fopen(JOYSTICK_CONFIG_FILE, "rb");
    if (!fp) {
        return false;
    }

    count = fgetc(fp);
    if (count <= 0) {
        fclose(fp);
        return false;
    }

    for (int i = 0; i < count; ++i) {
        joystick_mapping_t mapping;

        if (fread(&mapping, sizeof(mapping), 1, fp) != 1) {
            fclose(fp);
            return false;
        }

        if (mapping.action_id >= ACTION_COUNT || mapping.field > GP_FIELD_BTN1) {
            continue;
        }

        s_button_mappings[mapping.action_id].gamepad_button = mapping.field;
        s_button_mappings[mapping.action_id].gamepad_mask = mapping.mask;
        s_button_mappings[mapping.action_id].gamepad_button2 = 0u;
        s_button_mappings[mapping.action_id].gamepad_mask2 = 0u;
    }

    fclose(fp);
    return true;
}

static bool action_pressed(uint8_t action_id)
{
    button_mapping_t *mapping = &s_button_mappings[action_id];

    if (key_is_down(mapping->keyboard_key)) {
        return true;
    }

    if (action_id == ACTION_BTN_START) {
        if (key_is_down(KEY_P) || key_is_down(KEY_ESC) || key_is_down(KEY_ENTER)) {
            return true;
        }
    }

    if (!gamepad_connected()) {
        return false;
    }

    if ((gamepad_field_value(mapping->gamepad_button) & mapping->gamepad_mask) != 0u) {
        return true;
    }

    if (mapping->gamepad_mask2 != 0u &&
        (gamepad_field_value(mapping->gamepad_button2) & mapping->gamepad_mask2) != 0u) {
        return true;
    }

    return false;
}

void input_init(void)
{
    xreg(0, 0, 0, KEYBOARD_INPUT);
    xreg(0, 0, 2, GAMEPAD_INPUT);

    reset_default_mappings();
    (void)load_button_mappings();
    memset(&s_last_actions, 0, sizeof(s_last_actions));
}

void input_poll(input_actions_t *actions)
{
    memset(actions, 0, sizeof(*actions));

    RIA.addr0 = KEYBOARD_INPUT;
    RIA.step0 = 1;
    for (uint8_t i = 0; i < 32u; ++i) {
        s_keystate[i] = RIA.rw0;
    }

    RIA.addr0 = GAMEPAD_INPUT;
    RIA.step0 = 1;
    s_gamepad0.dpad = RIA.rw0;
    s_gamepad0.sticks = RIA.rw0;
    s_gamepad0.btn0 = RIA.rw0;
    s_gamepad0.btn1 = RIA.rw0;
    s_gamepad0.lx = (int8_t)RIA.rw0;
    s_gamepad0.ly = (int8_t)RIA.rw0;
    s_gamepad0.rx = (int8_t)RIA.rw0;
    s_gamepad0.ry = (int8_t)RIA.rw0;
    s_gamepad0.l2 = RIA.rw0;
    s_gamepad0.r2 = RIA.rw0;

    actions->up = (unsigned char)action_pressed(ACTION_MOVE_UP);
    actions->down = (unsigned char)action_pressed(ACTION_MOVE_DOWN);
    actions->left = (unsigned char)action_pressed(ACTION_MOVE_LEFT);
    actions->right = (unsigned char)action_pressed(ACTION_MOVE_RIGHT);
    actions->flip = (unsigned char)action_pressed(ACTION_BTN_Y);
    actions->land = (unsigned char)action_pressed(ACTION_BTN_SELECT);
    actions->fire = (unsigned char)action_pressed(ACTION_BTN_X);
    actions->bomb = (unsigned char)action_pressed(ACTION_BTN_A);
    actions->start = (unsigned char)action_pressed(ACTION_BTN_START);

    s_last_actions = *actions;
}

const input_actions_t *input_last_actions(void)
{
    return &s_last_actions;
}