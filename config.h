#ifndef CONFIG_H
#define CONFIG_H

#define CONFIG_SHELL "aesh"

#define CONFIG_CURSOR_COLOR (Color){0xFF,0xFF,0xFF,0xFF}
#define CONFIG_BGCOLOR      (Color){0x08,0x08,0x08,0xFF}
#define CONFIG_FGCOLOR      (Color){0xF4,0xF4,0xFF,0xFF}

#define CHAR_WIDTH  9
#define CHAR_HEIGHT 24

#define FONT_SIZE 24

#define COLS 160
#define ROWS 35
/* #define FONT_PATH "./aefont.otf" */
#define FONT_PATH "./IosevkaNerdFont-Regular.ttf"

Color pal16[16] = {
  {0, 0, 0, 255},       // 30: black
  {170, 0, 0, 255},     // 31: red
  {0, 170, 0, 255},     // 32: green
  {170, 170, 0, 255},   // 33: yellow
  {0, 0, 170, 255},     // 34: blue
  {170, 0, 170, 255},   // 35: magenta
  {0, 170, 170, 255},   // 36: cyan
  {170, 170, 170, 255}, // 37: white
  {85, 85, 85, 255},    // 90: bright black
  {255, 85, 85, 255},   // 91: bright red
  {85, 255, 85, 255},   // 92: bright green
  {255, 255, 85, 255},  // 93: bright yellow
  {85, 85, 255, 255},   // 94: bright blue
  {255, 85, 255, 255},  // 95: bright magenta
  {85, 255, 255, 255},  // 96: bright cyan
  {255, 255, 255, 255}  // 97: bright white
};

#endif /* CONFIG_H */

