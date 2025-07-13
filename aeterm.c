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

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

#define SCREEN_WIDTH    1066
#define SCREEN_HEIGHT   600

#define CHAR_WIDTH  10
#define CHAR_HEIGHT 17

#define FONT_PATH "/usr/share/fonts/gnu-free/FreeMonoBold.otf"
#define SIZE 24

#define ROWS 25
#define COLS 80
#define SCROLLBACK_ROWS 1000  // Буфер прокрутки

int master_fd;
typedef struct {
  char c;
  SDL_Color fg;
  SDL_Color bg;
} TermChar;

TermChar term[SCROLLBACK_ROWS][COLS];
int cursor_x = 0;
int cursor_y = 0;
int scroll_offset = 0;  // Смещение для прокрутки
int term_height = ROWS; // Видимая высота терминала
bool auto_scroll = true; // Автоматическая прокрутка

SDL_Color default_fg = {255, 255, 255, 255};
SDL_Color default_bg = {0, 0, 0, 255};
SDL_Color current_fg = {255, 255, 255, 255};
SDL_Color current_bg = {0, 0, 0, 255};

void init_term() {
  int y;
  for (y = 0; y < SCROLLBACK_ROWS; y++) {
    int x;
    for (x = 0; x < COLS; x++) {
      term[y][x].c = ' ';
      term[y][x].fg = default_fg;
      term[y][x].bg = default_bg;
    }
  }
}

void scroll_up() {
  if (scroll_offset < SCROLLBACK_ROWS - ROWS) {
    scroll_offset++;
  } else {
    // Сдвигаем все строки вверх, освобождая место внизу
    memmove(&term[0][0], &term[1][0], (SCROLLBACK_ROWS - 1) * COLS * sizeof(TermChar));
    cursor_y = SCROLLBACK_ROWS - 1;
    int x;
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

void render_char(SDL_Renderer* renderer, TTF_Font* font, TermChar tc, int x, int y) {
  char str[2] = {tc.c, '\0'};
  SDL_Surface* surf = TTF_RenderText_Blended(font, str, tc.fg);
  if (!surf) return;

  SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
  SDL_Rect dst = {x * CHAR_WIDTH, y * CHAR_HEIGHT, CHAR_WIDTH, CHAR_HEIGHT};
  SDL_SetRenderDrawColor(renderer, tc.bg.r, tc.bg.g, tc.bg.b, tc.bg.a);
  SDL_RenderFillRect(renderer, &dst);
  SDL_RenderCopy(renderer, tex, NULL, &dst);

  SDL_FreeSurface(surf);
  SDL_DestroyTexture(tex);
}

void render_term(SDL_Renderer* renderer, TTF_Font* font) {
  int y;
  for (y = 0; y < ROWS; y++) {
    int term_row = y + scroll_offset;
    if (term_row >= SCROLLBACK_ROWS) continue;
    int x;
    for (x = 0; x < COLS; x++) {
      render_char(renderer, font, term[term_row][x], x, y);
    }
  }
}

void handle_escape_sequence(const char* seq) {
  // Обработка ANSI escape-последовательностей
  if (strstr(seq, "[H")) {
    // Курсор в начало
    cursor_x = 0;
    cursor_y = 0;
  } else if (strstr(seq, "[2J")) {
    // Очистка экрана
    int y;
    for (y = 0; y < SCROLLBACK_ROWS; y++) {
      int x;
      for (x = 0; x < COLS; x++) {
        term[y][x].c = ' ';
        term[y][x].fg = default_fg;
        term[y][x].bg = default_bg;
      }
    }
    cursor_x = 0;
    cursor_y = 0;
    scroll_offset = 0;
  } else if (strstr(seq, "m")) {
    // Цвета
    int code = atoi(seq + 2);
    if (code >= 30 && code <= 37) {
      // Основные цвета текста
      static const SDL_Color fg_colors[] = {
        {0, 0, 0, 255},       // 30: black
        {170, 0, 0, 255},     // 31: red
        {0, 170, 0, 255},     // 32: green
        {170, 170, 0, 255},    // 33: yellow
        {0, 0, 170, 255},     // 34: blue
        {170, 0, 170, 255},   // 35: magenta
        {0, 170, 170, 255},   // 36: cyan
        {170, 170, 170, 255}  // 37: white
      };
      current_fg = fg_colors[code - 30];
    } else if (code >= 90 && code <= 97) {
      // Яркие цвета текста
      static const SDL_Color bright_fg_colors[] = {
        {85, 85, 85, 255},    // 90: bright black
        {255, 85, 85, 255},   // 91: bright red
        {85, 255, 85, 255},   // 92: bright green
        {255, 255, 85, 255},  // 93: bright yellow
        {85, 85, 255, 255},  // 94: bright blue
        {255, 85, 255, 255}, // 95: bright magenta
        {85, 255, 255, 255}, // 96: bright cyan
        {255, 255, 255, 255} // 97: bright white
      };
      current_fg = bright_fg_colors[code - 90];
    } else if (code >= 40 && code <= 47) {
      // Основные цвета фона
      static const SDL_Color bg_colors[] = {
        {0, 0, 0, 255},       // 40: black
        {170, 0, 0, 255},     // 41: red
        {0, 170, 0, 255},     // 42: green
        {170, 170, 0, 255},   // 43: yellow
        {0, 0, 170, 255},     // 44: blue
        {170, 0, 170, 255},   // 45: magenta
        {0, 170, 170, 255},   // 46: cyan
        {170, 170, 170, 255}  // 47: white
      };
      current_bg = bg_colors[code - 40];
    } else if (code >= 100 && code <= 107) {
      // Яркие цвета фона
      static const SDL_Color bright_bg_colors[] = {
        {85, 85, 85, 255},    // 100: bright black
        {255, 85, 85, 255},   // 101: bright red
        {85, 255, 85, 255},   // 102: bright green
        {255, 255, 85, 255},  // 103: bright yellow
        {85, 85, 255, 255},  // 104: bright blue
        {255, 85, 255, 255}, // 105: bright magenta
        {85, 255, 255, 255}, // 106: bright cyan
        {255, 255, 255, 255} // 107: bright white
      };
      current_bg = bright_bg_colors[code - 100];
    } else if (code == 0) {
      // Сброс атрибутов
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

    // Escape-последовательность завершается буквой
    if (isalpha(c) || c == '~') {
      handle_escape_sequence(esc_buffer);
      in_esc = false;
      esc_pos = 0;
    } else if (esc_pos >= sizeof(esc_buffer) - 1) {
      // Переполнение буфера
      in_esc = false;
      esc_pos = 0;
    }
    return;
  }

  if (c == '\x1b') {  // Начало escape-последовательности
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
    // Ctrl+A - Ctrl+Z: отправляем соответствующий управляющий символ
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

  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    printf("SDL2 could not be initialized!\nSDL2 Error: %s\n", SDL_GetError());
    return 1;
  }

  if (TTF_Init() < 0) {
    printf("SDL2_ttf could not be initialized!\nSDL2_ttf Error: %s\n", TTF_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_Window *window = SDL_CreateWindow("SDL2 Terminal", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
  if (!window) {
    printf("Window could not be created!\nSDL_Error: %s\n", SDL_GetError());
    TTF_Quit();
    SDL_Quit();
    return 1;
  }

  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer) {
    printf("Renderer could not be created!\nSDL_Error: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 1;
  }

  TTF_Font *font = TTF_OpenFont("FreeMonoBold.otf", SIZE);
  if (!font) {
    printf("fuck you!!!!\n"); // memory leak
    return 1;
  }

  init_term();

  // Создаем псевдотерминал (PTY) для bash
  pid_t pid = forkpty(&master_fd, NULL, NULL, NULL);
  if (pid < 0) {
    perror("forkpty");
    return 1;
  }

  if (pid == 0) {
    // Дочерний процесс: запускаем shell
    execlp("bash", "bash", NULL);
    perror("execlp");
    exit(1);
  }

  // Устанавливаем неблокирующий режим для master_fd
  fcntl(master_fd, F_SETFL, O_NONBLOCK);

  // Устанавливаем размер терминала
  #ifndef __OpenBSD__
  handle_sigwinch(0);
  #endif

  bool quit = false;
  while (!quit) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) {
        quit = true;
      } else if (e.type == SDL_TEXTINPUT) {
        write(master_fd, e.text.text, strlen(e.text.text));
      } else if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_RETURN) {
          write(master_fd, "\n", 1);
        } else if (e.key.keysym.sym == SDLK_BACKSPACE) {
          write(master_fd, "\b", 1);
        } else if (e.key.keysym.sym == SDLK_UP) {
          auto_scroll = false;
          if (scroll_offset < SCROLLBACK_ROWS - ROWS) {
            scroll_offset++;
          }
        } else if (e.key.keysym.sym == SDLK_DOWN) {
          auto_scroll = (scroll_offset == 0);
          if (scroll_offset > 0) {
            scroll_offset--;
          }
        } else if (e.key.keysym.mod & KMOD_CTRL) {
          // Обработка Ctrl+буква (A-Z)
          if (e.key.keysym.sym >= SDLK_a && e.key.keysym.sym <= SDLK_z) {
            char c = e.key.keysym.sym - SDLK_a + 1;
            write(master_fd, &c, 1);
          } else if (e.key.keysym.sym == SDLK_l) {
            auto_scroll = !auto_scroll;
          }
        }
      }
    }

    // Читаем вывод из shell
    char buf[1024];
    ssize_t n = read(master_fd, buf, sizeof(buf));
    if (n > 0) {
      ssize_t i;
      for (i = 0; i < n; i++) {
        handle_input(buf[i]);
        ensure_visible();
      }
    } else if (n < 0 && errno != EAGAIN) {
      perror("read");
      quit = true;
    }

    // Отрисовка
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    render_term(renderer, font);
    SDL_RenderPresent(renderer);

    // SDL_Delay(10);
  }

  // Завершение
  close(master_fd);
  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  TTF_CloseFont(font);
  TTF_Quit();
  SDL_Quit();

  return 0;
}

