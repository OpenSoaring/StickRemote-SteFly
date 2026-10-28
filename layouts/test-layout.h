const char layout_test_name[] PROGMEM = "Test-Layout";

const PROGMEM struct key_struct layout_test[btnSize] = {
/* Fn  */  { {0,'F'}, {0,'1'}, {0,'2'}},   
/* M   */  { {0,'M'}, {0,'3'}, {0,'4'}},   
/* X   */  { {0,'X'}, {0,'5'}, {0,'6'}},   
/* STF */  { {0,'S'}, {0,'7'}, {0,'8'}},   
/* Enter*/ { {0,KEY_RETURN},      KEY_STICK_RESET, {0,'9'}},  // joystick button
/* Up */   { {0,KEY_UP_ARROW},    {0, 0}, {0, -4}},
/* Down */ { {0,KEY_DOWN_ARROW},  {0, 0}, {0, +4}},
/* Left */ { {0,KEY_LEFT_ARROW},  {0,-4}, {0,  0}},
/* Right */{ {0,KEY_RIGHT_ARROW}, {0,+4}, {0,  0}},

/* PTT */  { -1 },   // btnPTT - not connected here
};
