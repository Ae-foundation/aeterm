#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>
#if defined(__OpenBSD__)
  #include <util.h>
  #include <utmp.h>
#elif defined(__FreeBSD__)
  #include <libutil.h>
#else
  #include <pty.h>
  #include <utmp.h>
#endif
#include <errno.h>
#include <signal.h>
#include <termios.h>

#include <raylib.h>
#include "config.h"

#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

#define SCREEN_WIDTH  (COLS*CHAR_WIDTH)
#define SCREEN_HEIGHT (ROWS*CHAR_HEIGHT)

#define rcury (MIN(cursor_y,ROWS-1))
#define SCROLLBACK_ROWS ROWS

int master_fd;
int cursor_hidden=0;

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

Color default_bg = CONFIG_BGCOLOR;
Color default_fg = CONFIG_FGCOLOR;

Color current_bg = CONFIG_BGCOLOR;
Color current_fg = CONFIG_FGCOLOR;

Font terminal_font;

Color xterm256[256] = {0};

void generate_xterm256_pal() {
  memcpy(xterm256, pal16, sizeof(Color)*16);
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
  if (auto_scroll) {
    if (cursor_y >= scroll_offset + ROWS) {
      scroll_offset = cursor_y - ROWS + 1;
    } else if (cursor_y < scroll_offset) {
      scroll_offset = cursor_y;  // This puts cursor at top line
    }
    if (scroll_offset < 0) scroll_offset = 0;
    if (scroll_offset > SCROLLBACK_ROWS - ROWS) {
      scroll_offset = SCROLLBACK_ROWS - ROWS;
    }
  }
}

void render_char(TermChar tc, int x, int y) {
  DrawRectangle(x * CHAR_WIDTH, y * CHAR_HEIGHT, CHAR_WIDTH, CHAR_HEIGHT, tc.bg);
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

#define render_cursor() \
  if (cursor_y >= scroll_offset && cursor_y < scroll_offset + ROWS) { \
    int visible_y = cursor_y - scroll_offset; \
    DrawRectangle(cursor_x * CHAR_WIDTH, visible_y * CHAR_HEIGHT, CHAR_WIDTH, CHAR_HEIGHT, CONFIG_CURSOR_COLOR); \
  }

int escx,escy;
#if defined(__FreeBSD__)
  int ra,ga,ba;
#else
  char ra,ga,ba;
#endif
void handle_escape_sequence(const char* seq) {
  if (strstr(seq, "H")) {
    if (sscanf(seq, "\033[%d;%dH",&escy,&escx) == 2) {
      cursor_x = MIN(escx,COLS)-1;
      cursor_y = MIN(escy,ROWS)-1;
    } else if (sscanf(seq, "\033[%dH", &escy) == 1) {
      cursor_y = MIN(escy,ROWS)-1;
    } else {
      cursor_x = 0;
      cursor_y = 0;
    }
  } else if (strstr(seq, "[2J")) {
    int x,y;
    for (y = 0; y < SCROLLBACK_ROWS; y++) {
      for (x = 0; x < COLS; x++) {
        term[y][x].c = ' ';
        term[y][x].fg = current_fg;
        term[y][x].bg = current_bg;
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
  } else if (strstr(seq, "38;2;")) {
    if (sscanf(seq, "\033[38;2;%d;%d;%dm", &ra,&ga,&ba) == 3) {
      current_fg = (Color){ra,ga,ba,0xFF};
    }
  } else if (strstr(seq, "48;2;")) {
    if (sscanf(seq, "\033[48;2;%d;%d;%dm", &ra,&ga,&ba) == 3) {
      current_bg = (Color){ra,ga,ba,0xFF};
    }
  } else if (strstr(seq, "48;5;")) {
    int col256 = atoi(seq + 7);
    if (col256) {
      current_bg = xterm256[col256];
    }
  } else if (!strncmp(seq, "\033]0;", 4)) {
    SetWindowTitle(seq+4);
    } else if (strstr(seq, "[K")) {
    int mode = 0;
    if (sscanf(seq, "\033[%dK", &mode) != 1) {
      mode = 0;
    }

    int start_x, end_x;
    switch (mode) {
      case 0: // Erase from cursor to end of line
        start_x = cursor_x;
        end_x = COLS - 1;
        break;
      case 1: // Erase from beginning of line to cursor
        start_x = 0;
        end_x = cursor_x;
        break;
      case 2: // Erase entire line
        start_x = 0;
        end_x = COLS - 1;
        break;
      default:
        start_x = cursor_x;
        end_x = COLS - 1;
        break;
    }

    int x;
    for (x = start_x; x <= end_x; x++) {
      term[cursor_y][x].c = ' ';
      term[cursor_y][x].fg = current_fg;
      term[cursor_y][x].bg = current_bg;
    }
  } else if (strstr(seq, "P")) {
    // Handle Delete Character sequence: \033[<n>P
    int n = 1, i, x;
    if (sscanf(seq, "\033[%dP", &n) != 1) {
      n = 1;
    }

    for (i = 0; i < n; i++) {
      if (cursor_x < COLS - 1) {
        for (x = cursor_x; x < COLS - 1; x++) {
          term[cursor_y][x] = term[cursor_y][x + 1];
        }
        term[cursor_y][COLS - 1].c = ' ';
        term[cursor_y][COLS - 1].fg = current_fg;
        term[cursor_y][COLS - 1].bg = current_bg;
      } else if (cursor_x == COLS - 1) {
        term[cursor_y][cursor_x].c = ' ';
        term[cursor_y][cursor_x].fg = current_fg;
        term[cursor_y][cursor_x].bg = current_bg;
      }
    }
  } else if (strstr(seq, "L")) {
    int n = 1, i, y, x;
    if (sscanf(seq, "\033[%dL", &n) != 1) n = 1;
    for (i = 0; i < n; i++) {
      for (y = SCROLLBACK_ROWS - 1; y > cursor_y; y--) {
        memmove(&term[y][0], &term[y-1][0], COLS * sizeof(TermChar));
      }
      for (x = 0; x < COLS; x++) {
        term[cursor_y][x].c = ' ';
        term[cursor_y][x].fg = current_fg;
        term[cursor_y][x].bg = current_bg;
      }
    }
  } else if (strstr(seq, "m")) {
    int code = atoi(seq + 2);
    
    if (code >= 30 && code <= 37) current_fg = xterm256[code-30];
    else if (code >= 90 && code <= 97) current_fg = xterm256[code-90 + 8];
    else if (code >= 40 && code <= 47) current_bg = xterm256[code-40];
    else if (code >= 100 && code <= 107) current_bg = xterm256[code-100 + 8];
    else if (code == 0) {
      current_fg = default_fg;
      current_bg = default_bg;
    }
  } else if (!strcmp(seq, "\033[M")) { /* Delete line (vim protocol) */
    int x;
    if (cursor_y < SCROLLBACK_ROWS - 1) {
      memmove(&term[cursor_y][0], &term[cursor_y + 1][0], (SCROLLBACK_ROWS - cursor_y - 1) * COLS * sizeof(TermChar));
    }
    for (x = 0; x < COLS; x++) {
      term[SCROLLBACK_ROWS - 1][x].c = ' ';
      term[SCROLLBACK_ROWS - 1][x].fg = default_fg;
      term[SCROLLBACK_ROWS - 1][x].bg = default_bg;
    }
  } else if (!strcmp(seq, "\033[?25h")) {
    cursor_hidden = 0;
  } else if (!strcmp(seq, "\033[?25l")) {
    cursor_hidden = 1;
  } else if (!strcmp(seq, "\033[6n")) {
    static char buf[16];
    int len = snprintf(buf, 16, "\033[%d;%dR", rcury+1, cursor_x+1);
    write(master_fd, buf, len);
  } else {
    printf("Unknown sequence \\%03o%s\n", *seq,seq+1);
  }
}

void handle_input(char c) {
  static char esc_buffer[64];
  static int esc_pos = 0;
  static bool in_esc, in_osc = false;

  if (in_esc && (c == ']' /*|| c == 'P'*/)) {
    in_osc = true;
    esc_buffer[0] = '\033';
    esc_buffer[1] = ']';
    esc_pos = 2;
    return;
  }
  if (in_osc) {
    esc_buffer[esc_pos++] = c;
    esc_buffer[esc_pos] = '\0';
    if ((c == '\x07') || (c == '\\')) {
      handle_escape_sequence(esc_buffer);
      in_osc = false;
      esc_pos = 0;
    } else if (esc_pos >= sizeof(esc_buffer) - 1) {
      in_osc = false;
      esc_pos = 0;
    }
    return;
  }
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
        /* scroll_offset = MAX(scroll_offset, cursor_y - ROWS + 1); */
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
  setenv("TERM", "ansi", 1);
  setenv("AETERM", "aeterm", 1);
  setenv("COLORTERM", "truecolor", 1);
  char lines_str[16], cols_str[16];
  snprintf(lines_str, sizeof(lines_str), "%d", ROWS);
  snprintf(cols_str, sizeof(cols_str), "%d", COLS);
  setenv("LINES", lines_str, 1);
  setenv("COLUMNS", cols_str, 1);
#ifndef __OpenBSD__
  signal(SIGWINCH, handle_sigwinch);
#endif

  InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "aeterm");
  SetTargetFPS(60);
  SetExitKey(KEY_NULL);

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
      write(master_fd, "\r", 1);
    } else if (IsKeyPressed(KEY_BACKSPACE)) {
      write(master_fd, "\x7F", 1);
    } else if (IsKeyPressed(KEY_TAB)) {
      write(master_fd, "\t", 1);
    } else if (IsKeyPressed(KEY_UP)) {
      write(master_fd, "\033[A", 3);
    } else if (IsKeyPressed(KEY_ESCAPE)) {
      write(master_fd, "\033", 1);
    } else if (IsKeyPressed(KEY_DOWN)) {
      write(master_fd, "\033[B", 3);
    } else if (IsKeyPressed(KEY_LEFT)) {
      write(master_fd, "\033[D", 3);
    } else if (IsKeyPressed(KEY_RIGHT)) {
      write(master_fd, "\033[C", 3);
    } else if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
      if (IsKeyPressed(KEY_A)) write(master_fd, "\x01", 1); if (IsKeyPressed(KEY_B)) write(master_fd, "\x02", 1);
      if (IsKeyPressed(KEY_C)) write(master_fd, "\x03", 1); if (IsKeyPressed(KEY_D)) write(master_fd, "\x04", 1);
      if (IsKeyPressed(KEY_E)) write(master_fd, "\x05", 1); if (IsKeyPressed(KEY_F)) write(master_fd, "\x06", 1);
      if (IsKeyPressed(KEY_G)) write(master_fd, "\x07", 1); if (IsKeyPressed(KEY_H)) write(master_fd, "\x08", 1);
      if (IsKeyPressed(KEY_I)) write(master_fd, "\x09", 1); if (IsKeyPressed(KEY_J)) write(master_fd, "\x0A", 1);
      if (IsKeyPressed(KEY_K)) write(master_fd, "\x0B", 1); if (IsKeyPressed(KEY_L)) write(master_fd, "\x0C", 1);
      if (IsKeyPressed(KEY_M)) write(master_fd, "\x0D", 1); if (IsKeyPressed(KEY_N)) write(master_fd, "\x0E", 1);
      if (IsKeyPressed(KEY_O)) write(master_fd, "\x0F", 1); if (IsKeyPressed(KEY_P)) write(master_fd, "\x10", 1);
      if (IsKeyPressed(KEY_Q)) write(master_fd, "\x11", 1); if (IsKeyPressed(KEY_R)) write(master_fd, "\x12", 1);
      if (IsKeyPressed(KEY_S)) write(master_fd, "\x13", 1); if (IsKeyPressed(KEY_T)) write(master_fd, "\x14", 1);
      if (IsKeyPressed(KEY_U)) write(master_fd, "\x15", 1); if (IsKeyPressed(KEY_V)) write(master_fd, "\x16", 1);
      if (IsKeyPressed(KEY_W)) write(master_fd, "\x17", 1); if (IsKeyPressed(KEY_X)) write(master_fd, "\x18", 1);
      if (IsKeyPressed(KEY_Y)) write(master_fd, "\x19", 1); if (IsKeyPressed(KEY_Z)) write(master_fd, "\x1A", 1);
    }

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

    // Master render
    BeginDrawing();
    ClearBackground(BLACK);
    render_term();
    if (!cursor_hidden) render_cursor();
    EndDrawing();
  }

  close(master_fd);
  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);

  UnloadFont(terminal_font);
  CloseWindow();

  return 0;
}

