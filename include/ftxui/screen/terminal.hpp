// Copyright 2020 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#ifndef FTXUI_SCREEN_TERMINAL_HPP
#define FTXUI_SCREEN_TERMINAL_HPP

#include <iostream>
#include <unistd.h>  // for STDIN_FILENO, STDOUT_FILENO

namespace ftxui {

  // The file descriptors of the currently active screen.
  extern int input_fd; // = STDIN_FILENO;
  extern int output_fd; // = STDOUT_FILENO;
  extern std::ostream* pcout; // = &std::cout;
  
  std::string CreatePsuedoTerminal(); 
  void ClosePsuedoTerminal(const std::string& pty_name);
  bool WaitForTerminalInput(int seconds);

struct Dimensions {
  int dimx;
  int dimy;
};

namespace Terminal {
Dimensions Size();
void SetFallbackSize(const Dimensions& fallbackSize);

enum Color {
  Palette1,
  Palette16,
  Palette256,
  TrueColor,
};
Color ColorSupport();
void SetColorSupport(Color color);

}  // namespace Terminal

}  // namespace ftxui

#endif  // FTXUI_SCREEN_TERMINAL_HPP
