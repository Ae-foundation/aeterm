#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>
#ifdef __OpenBSD__
  #include <util.h>
#else
  #include <pty.h>
#endif
#include <utmp.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>

#include <raylib.h>
#include "config.h"

#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

#define SCREEN_WIDTH  1066
#define SCREEN_HEIGHT   600

#define CHAR_WIDTH  8
#define CHAR_HEIGHT 16

#define FONT_SIZE 16

#define ROWS 35
#define COLS 106
#define SCROLLBACK_ROWS 1000

int master_fd;

typedef struct {
  char c;
  Color fg;
  Color bg;
} TermChar;

TermChar term[SCROLLBACK_ROWS][COLS];
int cursor_x = 0;
int cursor_y = 0;
int scroll_offset = 0;
int term_height = ROWS;
bool auto_scroll = true;

Color default_fg = WHITE;
Color default_bg = BLACK;
Color current_fg = WHITE;
Color current_bg = BLACK;

Font terminal_font;

Color fg_colors[] = {
  BLACK,      // 30: black
  {170, 0, 0, 255},   // 31: red
  {0, 170, 0, 255},   // 32: green
  {170, 170, 0, 255},   // 33: yellow
  {0, 0, 170, 255},   // 34: blue
  {170, 0, 170, 255},   // 35: magenta
  {0, 170, 170, 255},   // 36: cyan
  {170, 170, 170, 255}  // 37: white
};
Color bright_fg_colors[] = {
  {85, 85, 85, 255},  // 90: bright black
  {255, 85, 85, 255},   // 91: bright red
  {85, 255, 85, 255},   // 92: bright green
  {255, 255, 85, 255},  // 93: bright yellow
  {85, 85, 255, 255},   // 94: bright blue
  {255, 85, 255, 255},  // 95: bright magenta
  {85, 255, 255, 255},  // 96: bright cyan
  WHITE         // 97: bright white
};
Color bg_colors[] = {
  BLACK,      // 40: black
  {170, 0, 0, 255},   // 41: red
  {0, 170, 0, 255},   // 42: green
  {170, 170, 0, 255},   // 43: yellow
  {0, 0, 170, 255},   // 44: blue
  {170, 0, 170, 255},   // 45: magenta
  {0, 170, 170, 255},   // 46: cyan
  {170, 170, 170, 255}  // 47: white
};
Color bright_bg_colors[] = {
  {85, 85, 85, 255},  // 100: bright black
  {255, 85, 85, 255},   // 101: bright red
  {85, 255, 85, 255},   // 102: bright green
  {255, 255, 85, 255},  // 103: bright yellow
  {85, 85, 255, 255},   // 104: bright blue
  {255, 85, 255, 255},  // 105: bright magenta
  {85, 255, 255, 255},  // 106: bright cyan
  WHITE         // 107: bright white
};
Color xterm256[256] = {0};

void generate_xterm256_pal() {
  memcpy(xterm256,   fg_colors,        sizeof(Color)*8);
  memcpy(xterm256+8, bright_fg_colors, sizeof(Color)*8);
  char x,y,z;
  for (z = 0; z < 6; z++) {
    for (y = 0; y < 6; y++) {
      for (x = 0; x < 6; x++) {
        xterm256[16+z*36+y*6+x] = (Color){0x024*z,0x024*y,0x024*x,0x0FF};
      }
    }
  }
}

void init_term() {
  int x,y;
  for (y = 0; y < SCROLLBACK_ROWS; y++) {
    for (x = 0; x < COLS; x++) {
      term[y][x].c = ' ';
      term[y][x].fg = default_fg;
      term[y][x].bg = default_bg;
    }
  }
}

void scroll_up() {
  int x;
  if (scroll_offset < SCROLLBACK_ROWS - ROWS) {
    scroll_offset++;
  } else {
    memmove(&term[0][0], &term[1][0], (SCROLLBACK_ROWS - 1) * COLS * sizeof(TermChar));
    cursor_y = SCROLLBACK_ROWS - 1;
    for (x = 0; x < COLS; x++) {
      term[cursor_y][x].c = ' ';
      term[cursor_y][x].fg = default_fg;
      term[cursor_y][x].bg = default_bg;
    }
  }
}

void ensure_visible() {
  if (auto_scroll && cursor_y >= scroll_offset + ROWS) {
    scroll_offset = cursor_y - ROWS + 1;
    if (scroll_offset < 0) scroll_offset = 0;
  }
}

void render_char(TermChar tc, int x, int y) {
  // Рисуем фон
  DrawRectangle(x * CHAR_WIDTH, y * CHAR_HEIGHT, CHAR_WIDTH, CHAR_HEIGHT, tc.bg);
  
  // Рисуем символ
  if (tc.c != ' ') {
    char str[2] = {tc.c, '\0'};
    DrawTextEx(terminal_font, str, (Vector2){x * CHAR_WIDTH, y * CHAR_HEIGHT}, FONT_SIZE, 0, tc.fg);
  }
}

void render_term() {
  int x,y;
  for (y = 0; y < ROWS; y++) {
    int term_row = y + scroll_offset;
    if (term_row >= SCROLLBACK_ROWS) continue;
    
    for (x = 0; x < COLS; x++) {
      render_char(term[term_row][x], x, y);
    }
  }
}

void handle_escape_sequence(const char* seq) {
  if (strstr(seq, "[H")) {
    cursor_x = 0;
    cursor_y = 0;
  } else if (strstr(seq, "[2J")) {
    int x,y;
    for (y = 0; y < SCROLLBACK_ROWS; y++) {
      for (x = 0; x < COLS; x++) {
        term[y][x].c = ' ';
        term[y][x].fg = default_fg;
        term[y][x].bg = default_bg;
      }
    }
    cursor_x = 0;
    cursor_y = 0;
    scroll_offset = 0;
  } else if (strstr(seq, "A")) {
    unsigned short n = atoi(seq + 2);
    if (n <= 0) n = 1;
    cursor_y = MAX(0, cursor_y - n);
  } else if (strstr(seq, "B")) {
    unsigned short n = atoi(seq + 2);
    if (n <= 0) n = 1;
    cursor_y = MIN(SCROLLBACK_ROWS - 1, cursor_y + n);
  } else if (strstr(seq, "C")) {
    unsigned short n = atoi(seq + 2);
    if (n <= 0) n = 1;
    cursor_x = MIN(COLS - 1, cursor_x + n);
  } else if (strstr(seq, "D")) {
    unsigned short n = atoi(seq + 2);
    if (n <= 0) n = 1;
    cursor_x = MAX(0, cursor_x - n);
  } else if (strstr(seq, "38;5;")) {
    int col256 = atoi(seq + 7);
    if (col256) {
      current_fg = xterm256[col256];
    }
  } else if (strstr(seq, "48;5;")) {
    int col256 = atoi(seq + 7);
    if (col256) {
      current_bg = xterm256[col256];
    }
  } else if (strstr(seq, "m")) {
    int code = atoi(seq + 2);
    
    // Цвета текста
    if (code >= 30 && code <= 37) {
      current_fg = fg_colors[code - 30];
    }
    else if (code >= 90 && code <= 97) {
      current_fg = bright_fg_colors[code - 90];
    }
    // Цвета фона
    else if (code >= 40 && code <= 47) {
      current_bg = bg_colors[code - 40];
    }
    else if (code >= 100 && code <= 107) {
      current_bg = bright_bg_colors[code - 100];
    }
    else if (code == 0) {
      current_fg = default_fg;
      current_bg = default_bg;
    }
  }
}

void handle_input(char c) {
  static char esc_buffer[32];
  static int esc_pos = 0;
  static bool in_esc = false;

  if (in_esc) {
    esc_buffer[esc_pos++] = c;
    esc_buffer[esc_pos] = '\0';

    if (isalpha(c) || c == '~') {
      handle_escape_sequence(esc_buffer);
      in_esc = false;
      esc_pos = 0;
    } else if (esc_pos >= sizeof(esc_buffer) - 1) {
      in_esc = false;
      esc_pos = 0;
    }
    return;
  }

  if (c == '\x1b') {
    in_esc = true;
    esc_buffer[0] = c;
    esc_pos = 1;
    return;
  }

  if (c == '\n') {
    cursor_x = 0;
    cursor_y++;
    if (cursor_y >= SCROLLBACK_ROWS) {
      scroll_up();
      scroll_offset = MAX(scroll_offset, cursor_y - ROWS + 1);
      cursor_y = SCROLLBACK_ROWS - 1;
    }
  } else if (c == '\t') {
    cursor_x += 7;
    cursor_x /= 8;
    cursor_x *= 8;
  } else if (c == '\r') {
    cursor_x = 0;
  } else if (c == '\b') {
    if (cursor_x > 0) {
      cursor_x--;
      term[cursor_y][cursor_x].c = ' ';
      term[cursor_y][cursor_x].fg = current_fg;
      term[cursor_y][cursor_x].bg = current_bg;
    }
  } else if (c >= 1 && c <= 26) {
    write(master_fd, &c, 1);
  } else {
    term[cursor_y][cursor_x].c = c;
    term[cursor_y][cursor_x].fg = current_fg;
    term[cursor_y][cursor_x].bg = current_bg;
    cursor_x++;
    if (cursor_x >= COLS) {
      cursor_x = 0;
      cursor_y++;
      if (cursor_y >= SCROLLBACK_ROWS) {
        scroll_up();
        scroll_offset = MAX(scroll_offset, cursor_y - ROWS + 1);
        cursor_y = SCROLLBACK_ROWS - 1;
      }
    }
  }
}

#ifndef __OpenBSD__
void handle_sigwinch(int sig) {
  struct winsize ws;
  ws.ws_row = ROWS;
  ws.ws_col = COLS;
  ioctl(master_fd, TIOCSWINSZ, &ws);
}
#endif

int main(int argc, char* argv[]) {
  setenv("TERM", "aeterm", 1);
#ifndef __OpenBSD__
  signal(SIGWINCH, handle_sigwinch);
#endif

  InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "aeterm");
  SetTargetFPS(60);

  terminal_font = LoadFont(FONT_PATH);
  if (terminal_font.texture.id == 0) {
    TraceLog(LOG_ERROR, "Failed to load font: %s", FONT_PATH);
    CloseWindow();
    return 1;
  }

  generate_xterm256_pal();
  init_term();

  pid_t pid = forkpty(&master_fd, NULL, NULL, NULL);
  if (pid < 0) {
    perror("forkpty");
    CloseWindow();
    return 1;
  }

  if (pid == 0) {
    execlp(CONFIG_SHELL, CONFIG_SHELL, NULL);
    perror(CONFIG_SHELL "n'existe pas");
    exit(1);
  }

  fcntl(master_fd, F_SETFL, O_NONBLOCK);
#ifndef __OpenBSD__
  handle_sigwinch(0);
#endif

  while (!WindowShouldClose()) {
    int key = GetCharPressed();
    while (key > 0) {
      char c = (char)key;
      write(master_fd, &c, 1);
      key = GetCharPressed();
    }

    if (IsKeyPressed(KEY_ENTER)) {
      write(master_fd, "\n", 1);
    } else if (IsKeyPressed(KEY_BACKSPACE)) {
      write(master_fd, "\x7F", 1);
    } else if (IsKeyPressed(KEY_TAB)) {
      write(master_fd, "\t", 1);
    } else if (IsKeyPressed(KEY_UP)) {
      write(master_fd, "\033[A", 3);
    } else if (IsKeyPressed(KEY_DOWN)) {
      write(master_fd, "\033[B", 3);
    } else if (IsKeyPressed(KEY_LEFT)) {
      write(master_fd, "\033[D", 3);
    } else if (IsKeyPressed(KEY_RIGHT)) {
      write(master_fd, "\033[C", 3);
    } else if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
      if (IsKeyPressed(KEY_A)) write(master_fd, "\x01", 1);
      if (IsKeyPressed(KEY_B)) write(master_fd, "\x02", 1);
      if (IsKeyPressed(KEY_C)) write(master_fd, "\x03", 1);
      if (IsKeyPressed(KEY_D)) write(master_fd, "\x04", 1);
      if (IsKeyPressed(KEY_E)) write(master_fd, "\x05", 1);
      if (IsKeyPressed(KEY_F)) write(master_fd, "\x06", 1);
      if (IsKeyPressed(KEY_G)) write(master_fd, "\x07", 1);
      if (IsKeyPressed(KEY_H)) write(master_fd, "\x08", 1);
      if (IsKeyPressed(KEY_I)) write(master_fd, "\x09", 1);
      if (IsKeyPressed(KEY_J)) write(master_fd, "\x0A", 1);
      if (IsKeyPressed(KEY_K)) write(master_fd, "\x0B", 1);
      if (IsKeyPressed(KEY_L)) {
        auto_scroll = !auto_scroll;
        write(master_fd, "\x0C", 1);
      }
      if (IsKeyPressed(KEY_M)) write(master_fd, "\x0D", 1);
      if (IsKeyPressed(KEY_N)) write(master_fd, "\x0E", 1);
      if (IsKeyPressed(KEY_O)) write(master_fd, "\x0F", 1);
      if (IsKeyPressed(KEY_P)) write(master_fd, "\x10", 1);
      if (IsKeyPressed(KEY_Q)) write(master_fd, "\x11", 1);
      if (IsKeyPressed(KEY_R)) write(master_fd, "\x12", 1);
      if (IsKeyPressed(KEY_S)) write(master_fd, "\x13", 1);
      if (IsKeyPressed(KEY_T)) write(master_fd, "\x14", 1);
      if (IsKeyPressed(KEY_U)) write(master_fd, "\x15", 1);
      if (IsKeyPressed(KEY_V)) write(master_fd, "\x16", 1);
      if (IsKeyPressed(KEY_W)) write(master_fd, "\x17", 1);
      if (IsKeyPressed(KEY_X)) write(master_fd, "\x18", 1);
      if (IsKeyPressed(KEY_Y)) write(master_fd, "\x19", 1);
      if (IsKeyPressed(KEY_Z)) write(master_fd, "\x1A", 1);
    }

    // Чтение вывода из shell
    char buf[1024];
    ssize_t n = read(master_fd, buf, sizeof(buf));
    int i;
    if (n > 0) {
      for (i = 0; i < n; i++) {
        handle_input(buf[i]);
        ensure_visible();
      }
    } else if (n < 0 && errno != EAGAIN) {
      perror("read");
      break;
    }

    // Отрисовка
    BeginDrawing();
    ClearBackground(BLACK);
    render_term();
    EndDrawing();
  }

  close(master_fd);
  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);

  UnloadFont(terminal_font);
  CloseWindow();

  return 0;
}

