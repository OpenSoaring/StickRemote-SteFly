const char layout_android2_name[] PROGMEM = "Android (w/ app switch)";

const PROGMEM struct key_struct layout_android2[btnSize] = {
    // Layout for Android w/ app switch and w/o mouse.
    //
    // Mixed-mode DblClick: only Fn and X have DblClick bindings (for the
    // Android-specific APP_SWITCH / APP_HIDE actions, which can't move to
    // LongClick because the LongClick slots there are already taken by
    // standard mappings F3 / 'X'). The other three buttons (M, X, Enter)
    // have DblClick = 0 -> eager Click, no 300ms latency on them.
    //
    // Net effect: snappy on M, X, Enter; small latency only when tapping
    // Fn or X (which are exactly the ones with extra Android function).
/* Fn  */  { {0,KEY_F2},          {0,KEY_F3},      KEY_APP_SWITCH2},
/* M   */  { {0,KEY_F1},          {0,'M'},         0},                // QuickMenu and AudioMenu
/* X   */  { {0,KEY_ESC},         {0,'X'},         0},     // ESC and 'X' for Shutdown (or Quit)
/* STF */  { KEY_TOGGLE_S_V,      {0,KEY_F7},      KEY_APP_HIDE},
/* Enter*/ { {0,KEY_RETURN},      KEY_STICK_RESET, 0},                // joystick button
/* Up */   { {0,KEY_UP_ARROW},    {0, 0}, {0, -4}},
/* Down */ { {0,KEY_DOWN_ARROW},  {0, 0}, {0, +4}},
/* Left */ { {0,KEY_LEFT_ARROW},  {0,-4}, {0,  0}},
/* Right */{ {0,KEY_RIGHT_ARROW}, {0,+4}, {0,  0}},

/* PTT */  { -1 },   // btnPTT - not connected here
};

