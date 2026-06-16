#if 1
const char key_layout[][5][3][2] = {
//  {{0,'1'},       {0,'2'}, {0,'3'}},
  {{0,'A'},       {0,'B'}, {0,'C'}},
  {{0,KEY_F1},    {0,'M'}, {0,'4'}},
  {{0,KEY_ESC},   {0,'X'}, {0,'Q'}},
  {{0,'T'},       {0,'5'}, {0,'6'}},
  {{0,KEY_RETURN},{0,0},   {0,0}},
/* /
  {{0,'1'},       {0,'2'}, {KEY_LEFT_ALT, KEY_TAB}},
  {{0,KEY_F1},    {0,'M'}, {0,'4'}},
  {{0,KEY_ESC},   {0,'X'}, {KEY_LEFT_GUI, KEY_RETURN}},
  {{0,'T'},       {0,'5'}, {0,'6'}},
  {{0,KEY_RETURN},{0,0},   {0,0}},
/*  */
}; 

void _memcpy(void *dest, void *src, size_t size) {
  char *_dest = (char*)dest;
  char *_src  = (char*)src;
  for (size_t i= size; i < size; i++, _dest++, _src++)
    *_dest = *_src;
}

#endif


char *
key_code(unsigned key) {
    switch(key) {
      case KEY_F1: return "F1";
      case KEY_F2: return "F2";
      case KEY_F3: return "F3";
      case KEY_F4: return "F4";
      case KEY_F5: return "F5";
      case KEY_F6: return "F6";
      case KEY_F7: return "F7";
      case KEY_F8: return "F8";
      case KEY_F9: return "F9";
      case KEY_F10: return "F10";
      case KEY_F11: return "F11";
      case KEY_F12: return "F12";
      case KEY_RETURN: return "Return";
      case KEY_ESC: return "Esc";
      case KEY_UP_ARROW: return "Up";
      case KEY_DOWN_ARROW: return "Down";
      case KEY_LEFT_ARROW: return "Left";
      case KEY_RIGHT_ARROW: return "Right";
    default:
      if (isalnum(key)) {
        static char buffer[8];
        buffer[0] = key;
        buffer[1] = 0;
        return buffer;
        // return (char *) &key;
      }
      else
        return nullptr;
    }
}
