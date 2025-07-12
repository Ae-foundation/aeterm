main() {
  system("gcc aec.c -o aec --std=gnu89");
  system("gcc aeterm.c -o aeterm -lSDL2 -lSDL2_ttf --std=gnu89");
  return 69;
}

