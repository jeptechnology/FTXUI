// Copyright 2020 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#include <cstdlib>  // for getenv
#include <string>   // for string, allocator

#include "ftxui/screen/terminal.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#else
#include <string.h>
#include <errno.h>
#include <cstdio>
#include <pty.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>  // for winsize, ioctl, TIOCGWINSZ
#include <unistd.h>     // for STDOUT_FILENO
#endif

#include <atomic>  // for atomic

namespace ftxui {

  class fdoutbuf : public std::streambuf 
  {
    protected:
      int fd;    // file descriptor
 
    public:
      // constructor
      fdoutbuf (int _fd) : fd(_fd) {}
    
    protected:
      // write one character
      virtual int_type overflow (int_type c) {
          if (c != EOF) {
              char z = c;
              if (write (fd, &z, 1) != 1) {
                  return EOF;
              }
          }
          return c;
      }
      
      // write multiple characters
      virtual
      std::streamsize xsputn (const char* s,
                              std::streamsize num) {
          auto written = write(fd,s,num);
          if (written < num) {
              return EOF;
          }
          return written;

      }
  };

  class fdostream : public std::ostream {
    protected:
      fdoutbuf buf;
    public:
      fdostream (int fd) : std::ostream(0), buf(fd) {
          rdbuf(&buf);
      }
  };    

  int input_fd = STDIN_FILENO;
  int output_fd = STDOUT_FILENO;
  int psuedo_fd[2];
  std::ostream* pcout = &std::cout;
 
  std::string CreatePsuedoTerminal(const std::string& pty_name)
  {    
    if (pty_name.empty())
    {
      char buf[256];
      struct termios tty;
      tty.c_iflag = (tcflag_t) 0;
      tty.c_lflag = (tcflag_t) 0;
      tty.c_cflag = CS8;
      tty.c_oflag = (tcflag_t) 0;

      auto e = openpty(&psuedo_fd[0], &psuedo_fd[1], buf, &tty, nullptr);
      if(0 > e) {
        std::printf("Error: %s\n", strerror(errno));
        return "";
      }

      // both our input and output should use the psuedo_fd[0]
      input_fd = psuedo_fd[0];
      output_fd = psuedo_fd[0];
      pcout = new fdostream(output_fd);
      return buf;
    }
    else
    {
       input_fd = open(pty_name.c_str(), O_RDWR | O_NONBLOCK);
       output_fd = input_fd;
       if (input_fd < 0)
       {
         return "";
       }
       pcout = new fdostream(output_fd);

      //  (*pcout) << "\033[2J"; //clear screen
      //  (*pcout) << "\033[9999;9999H"; // cursor should move as far as it can
      //  (*pcout) << "\033[6n"; // ask for cursor position            
       
       std::printf("Connected to PTY: %s\r\n", pty_name.c_str());

       return pty_name;
    }
  }

  void ClosePsuedoTerminal(const std::string& pty_name)
  {
    if (pcout != &std::cout)
    {
      delete pcout;
      close(psuedo_fd[1]);
      close(psuedo_fd[0]);
    }
  }

  bool WaitForTerminalInput(int seconds)
  {
    timeval tv = {seconds, 0};
    fd_set fds;
    FD_ZERO(&fds);                                     // NOLINT
    FD_SET(input_fd, &fds);                             // NOLINT
    select(input_fd + 1, &fds, nullptr, nullptr, &tv);  // NOLINT
    return FD_ISSET(input_fd, &fds);                    // NOLINT
  }

  int  GetCharOnTerminal(unsigned timeoutMilliseconds)
  {
    timeval tv = {timeoutMilliseconds / 1000, (timeoutMilliseconds % 1000) * 1000};
    fd_set fds;
    FD_ZERO(&fds);                                     // NOLINT
    FD_SET(input_fd, &fds);                             // NOLINT
    select(input_fd + 1, &fds, nullptr, nullptr, &tv);  // NOLINT
    if (FD_ISSET(input_fd, &fds))                       // NOLINT
    {
      char ch;
      if (1 == read(input_fd, &ch, 1))
      {
        return ch;
      }
    }
    return EOF;  
  }


namespace {

bool g_cached = false;                     // NOLINT
Terminal::Color g_cached_supported_color;  // NOLINT

Dimensions& FallbackSize() {
#if defined(__EMSCRIPTEN__)
  // This dimension was chosen arbitrarily to be able to display:
  // https://arthursonzogni.com/FTXUI/examples
  // This will have to be improved when someone has time to implement and need
  // it.
  constexpr int fallback_width = 140;
  constexpr int fallback_height = 43;
#else
  // The terminal size in VT100 was 80x24. It is still used nowadays by
  // default in many terminal emulator. That's a good choice for a fallback
  // value.
  constexpr int fallback_width = 80;
  constexpr int fallback_height = 24;
#endif
  static Dimensions g_fallback_size{
      fallback_width,
      fallback_height,
  };
  return g_fallback_size;
}

Dimensions& GetPsuedoTerminalSize(bool force = false) {
  static Dimensions g_psuedo_size
  {
      0,
      0,
  };

  if (!force && g_psuedo_size.dimx != 0 && g_psuedo_size.dimy != 0) 
  {
    return g_psuedo_size;  // already set
  }

  (*pcout) << "\0337\033[r\033[999;999H\033[6n\0338";

  std::string input;
  int ch = GetCharOnTerminal(1000);

  while (ch > 0 && ch != 'R')  // R terminates the response
  {
    if (EOF == ch || input.size() > 100) {
      break;
    }
    if (isprint(ch)) {
      input.push_back(ch);
    }
    ch = GetCharOnTerminal(1000);
  }

  (*pcout) << "\033[18t";  // move to upper left corner

  if (2 == sscanf(input.c_str(), "[%d;%d", &g_psuedo_size.dimy,
                  &g_psuedo_size.dimx)) {
    return g_psuedo_size;
  } else {
    return FallbackSize();
  }
}

const char* Safe(const char* c) {
  return (c != nullptr) ? c : "";
}

bool Contains(const std::string& s, const char* key) {
  return s.find(key) != std::string::npos;
}

Terminal::Color ComputeColorSupport() {
#if defined(__EMSCRIPTEN__)
  return Terminal::Color::TrueColor;
#endif

  std::string COLORTERM = Safe(std::getenv("COLORTERM"));  // NOLINT
  if (Contains(COLORTERM, "24bit") || Contains(COLORTERM, "truecolor")) {
    return Terminal::Color::TrueColor;
  }

  std::string TERM = Safe(std::getenv("TERM"));  // NOLINT
  if (Contains(COLORTERM, "256") || Contains(TERM, "256")) {
    return Terminal::Color::Palette256;
  }

#if defined(FTXUI_MICROSOFT_TERMINAL_FALLBACK)
  // Microsoft terminals do not properly declare themselve supporting true
  // colors: https://github.com/microsoft/terminal/issues/1040
  // As a fallback, assume microsoft terminal are the ones not setting those
  // variables, and enable true colors.
  if (TERM.empty() && COLORTERM.empty()) {
    return Terminal::Color::TrueColor;
  }
#endif

  return Terminal::Color::Palette16;
}

}  // namespace

namespace Terminal {

/// @brief Get the terminal size.
/// @return The terminal size.
/// @ingroup screen
Dimensions Size() {
#if defined(__EMSCRIPTEN__)
  // This dimension was chosen arbitrarily to be able to display:
  // https://arthursonzogni.com/FTXUI/examples
  // This will have to be improved when someone has time to implement and need
  // it.
  return FallbackSize();
#elif defined(_WIN32)
  CONSOLE_SCREEN_BUFFER_INFO csbi;

  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
    return Dimensions{csbi.srWindow.Right - csbi.srWindow.Left + 1,
                      csbi.srWindow.Bottom - csbi.srWindow.Top + 1};
  }

  return FallbackSize();
#else
  // if (!isatty(output_fd) == 0) 
  // {
  //    return GetPsuedoTerminalSize();
  // }
  
  winsize w{};
  const int status = ioctl(output_fd, TIOCGWINSZ, &w);  // NOLINT
  // The ioctl return value result should be checked. Some operating systems
  // don't support TIOCGWINSZ.
  if (w.ws_col == 0 || w.ws_row == 0 || status < 0) {
    return FallbackSize();
  }
  return Dimensions{w.ws_col, w.ws_row};
#endif
}

/// @brief Override terminal size in case auto-detection fails
/// @param fallbackSize Terminal dimensions to fallback to
void SetFallbackSize(const Dimensions& fallbackSize) {
  FallbackSize() = fallbackSize;
}

/// @brief Get the color support of the terminal.
/// @ingroup screen
Color ColorSupport() {
  if (!g_cached) {
    g_cached = true;
    g_cached_supported_color = ComputeColorSupport();
  }
  return g_cached_supported_color;
}

/// @brief Override terminal color support in case auto-detection fails
/// @ingroup dom
void SetColorSupport(Color color) {
  g_cached = true;
  g_cached_supported_color = color;
}

}  // namespace Terminal
}  // namespace ftxui
