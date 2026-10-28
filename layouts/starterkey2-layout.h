const char layout_starterkey2_name[] PROGMEM = "Starter-Key (w/ app switch)";

const PROGMEM struct key_struct layout_starterkey2[btnSize] = {
    // Layout for sticks w/o Fn button (Fn is the engine starter key).
    // Primary target: SteFlyNav on Android - so APP_SWITCH and APP_HIDE are
    // wired in via DblClick (LongClick slots are taken by standard mappings).
    //
    // Mixed-mode DblClick: only X and STF have DblClicks for Android-specific
    // actions. M and Enter stay eager (no 300ms latency). Fn is disabled.
/* Fn  */  { -1},                                                  // engine starter key, button hidden
/* M   */  { {0,KEY_F1},          {0,'M'},          KEY_APP_SWITCH2},            // QuickMenu and AudioMenu
/* X   */  { {0,KEY_ESC},         {0,'X'},          0}, // ESC and 'X' for Shutdown (or Quit)
/* STF */  { KEY_TOGGLE_S_V,      KEY_MOUSE_SWITCH, KEY_APP_HIDE},
/* Enter*/ { {0,KEY_RETURN},      KEY_STICK_RESET,  0},             // joystick button
/* Up */   { {0,KEY_UP_ARROW},    {0, 0}, {0, -4}},
/* Down */ { {0,KEY_DOWN_ARROW},  {0, 0}, {0, +4}},
/* Left */ { {0,KEY_LEFT_ARROW},  {0,-4}, {0,  0}},
/* Right */{ {0,KEY_RIGHT_ARROW}, {0,+4}, {0,  0}},

/* PTT */  { -1 },   // btnPTT - not connected here
};
