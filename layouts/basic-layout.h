const PROGMEM struct key_struct layout_basic[btnSize] = {
/* Fn  */  { KEY_MOUSE_SWITCH,    KEY_MOUSE_SWITCH, 0},   
/* M   */  { {0,KEY_F1},          {0,'M'}, 0 },         // QuickMenu and AudioMenu
/* X   */  { {0,KEY_ESC},         {0,'X'}, 0 },         // ESC and 'X' for Shutdown (or Quit)
/* STF */  { KEY_TOGGLE_S_V,      KEY_TOGGLE_S_V,  0},
/* Enter*/ { {0,KEY_RETURN},      KEY_STICK_RESET, 0},  // joystick button
/* Up */   { {0,KEY_UP_ARROW},    {0, 0}, {0, -4}},
/* Down */ { {0,KEY_DOWN_ARROW},  {0, 0}, {0, +4}},
/* Left */ { {0,KEY_LEFT_ARROW},  {0,-4}, {0,  0}},
/* Right */{ {0,KEY_RIGHT_ARROW}, {0,+4}, {0,  0}},

/* PTT */  { -1 },   // btnPTT - not connected here
};
