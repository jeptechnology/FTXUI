// Copyright 2020 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#ifndef FTXUI_SCREEN_TERMINAL_HPP
#define FTXUI_SCREEN_TERMINAL_HPP

#include <iostream>
#include <unistd.h>  // for STDIN_FILENO, STDOUT_FILENO

namespace ftxui {

struct Dimensions {
  int dimx;
  int dimy;
};

class Terminal {
public:
  static Terminal& Current();   
  static Terminal  Create(int inputFd, int outputFd);
  
  Terminal(const Terminal&) = delete;
  Terminal& operator=(const Terminal&) = delete;
  Terminal(Terminal&&) = default;
  Terminal& operator=(Terminal&&) = default;
  
  Dimensions Size();
  void SetFallbackSize(const Dimensions& fallbackSize);

  enum Color {
    Palette1,
    Palette16,
    Palette256,
    TrueColor,
  };

  bool WaitForTerminalInput(int seconds);
  size_t Read(char *buffer, size_t buffer_size);  // NOLINT

  Color ColorSupport();
  void SetColorSupport(Color color);
  ~Terminal();

private:
  Terminal(int input_fd, int output_fd);

  // The file descriptors of the currently active screen.
  int m_input_fd; // default = STDIN_FILENO;
  int m_output_fd; // default = STDOUT_FILENO;
  std::ostream* pcout; // = &std::cout;
  std::string pty_name;
  bool g_cached = false;                     // NOLINT
  Color g_cached_supported_color;  // NOLINT

  std::string CreatePsuedoTerminal(); 
  void ClosePsuedoTerminal(const std::string& pty_name);

public:
  std::ostream& output;

};  // class Terminal

}  // namespace ftxui

#endif  // FTXUI_SCREEN_TERMINAL_HPP
