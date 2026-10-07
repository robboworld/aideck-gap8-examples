#ifndef LETTER20_COMMANDS_H
#define LETTER20_COMMANDS_H

/* Must match tinyml_demo/letter20.py LETTER20_CLASS_NAMES order (v2). */
static const char *letter20_names[20] = {
    "A", "C", "E", "F", "H",
    "K", "L", "M", "N", "P",
    "R", "S", "T", "U", "V",
    "W", "X", "Y", "Z", "empty",
};

static const char *letter20_commands[20] = {
    "ascend", "circle", "emergency_land", "forward", "hover",
    "keep", "left", "mode_toggle", "neutral", "photo",
    "right", "stop_land", "takeoff", "up", "yaw_reverse",
    "wait", "cancel", "yaw_left", "yaw_right", "none",
};

#endif
