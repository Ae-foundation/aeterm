main() {
  system("cc aec.c -o aec --std=gnu89");
  #ifdef __OpenBSD__
  system("cc aeterm.c -o aeterm -I/usr/local/include -L/usr/local/lib/ -lSDL2 -lSDL2_ttf --std=gnu89 -lutil");
  #else
  system("cc aeterm.c -o aeterm -lSDL2 -lSDL2_ttf --std=gnu89");
  #endif
  return 69;
}

