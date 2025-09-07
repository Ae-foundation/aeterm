main() {
  system("cc aec.c -o aec --std=gnu89");
  #if defined(__OpenBSD__)
    system("cc aeterm.c -o aeterm -I/usr/local/include -L/usr/local/lib/ -lraylib -lm -std=gnu89 -lutil");
  #elif defined(__FreeBSD__)
    system("cc aeterm.c -o aeterm -I/usr/local/include -L/usr/local/lib/ -lraylib -lm -std=gnu89 -lutil");
  #else
    system("cc aeterm.c -o aeterm -lraylib -lm -std=gnu89");
  #endif
  return 69;
}

