const PROGMEM struct key_struct layout_android[btnSize] = {
    // ... layout for android w/ app switch and w/o mouse 
/* Fn  */  { {0,KEY_F2},          {0,KEY_F3}, KEY_APP_SWITCH},   
/* M   */  { {0,KEY_F1},          {0,'M'},{0,KEY_F5}},   // QuickMenu and AudioMenu
/* X   */  { {0,KEY_ESC},         {0,'X'},KEY_APP_HIDE}, // ESC and 'X' for Shutdown (or Quit)
/* STF */  { KEY_TOGGLE_S_V,      {0,KEY_F7}, {0,KEY_F8}},   
/* Enter*/ { {0,KEY_RETURN},      KEY_STICK_RESET, {0,KEY_F10}},  // joystick button
/* Up */   { {0,KEY_UP_ARROW},    {0, 0}, {0, -4}},
/* Down */ { {0,KEY_DOWN_ARROW},  {0, 0}, {0, +4}},
/* Left */ { {0,KEY_LEFT_ARROW},  {0,-4}, {0,  0}},
/* Right */{ {0,KEY_RIGHT_ARROW}, {0,+4}, {0,  0}},

/* PTT */  { -1 },   // btnPTT - not connected here
};

