#ifndef LETTER20_COMMANDS_H
#define LETTER20_COMMANDS_H

/* Must match tinyml_demo/letter20.py LETTER20_CLASS_NAMES order. */
static const char *letter20_names[20] = {
    "U", "D", "L", "R", "F",
    "B", "H", "S", "T", "C",
    "P", "W", "G", "V", "Y",
    "Z", "E", "N", "M", "X",
};

static const char *letter20_commands[20] = {
    "up", "down", "left", "right", "forward",
    "back", "hover", "stop_land", "takeoff", "circle",
    "photo", "wait", "go", "yaw_reverse", "yaw_left",
    "yaw_right", "emergency_land", "neutral", "mode_toggle", "cancel",
};

#endif
