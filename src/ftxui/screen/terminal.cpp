// Copyright 2020 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#include <cstdlib>  // for getenv
#include <string>   // for string, allocator

#include "ftxui/screen/terminal.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include <sys/select.h>  // for select, FD_ISSET, FD_SET, FD_ZERO, fd_set, timeval
#if !defined(ESP32)
#  include <termios.h>  // for tcsetattr, termios, tcgetattr, TCSANOW, cc_t, ECHO, ICANON, VMIN, VTIME
#endif
#include <unistd.h>  // for STDIN_FILENO, read

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
#if !defined(ESP32)
#  include <pty.h>
#  include <termios.h>
#  include <sys/ioctl.h>  // for winsize, ioctl, TIOCGWINSZ
#endif
#include <unistd.h>     // for STDOUT_FILENO
#endif


namespace
{
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
          return write(fd,s,num);
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

  ftxui::Terminal* g_currentTerminal;
}

namespace ftxui {

  Terminal::Terminal(int input_fd, int output_fd)
    : m_input_fd(input_fd)
    , m_output_fd(output_fd)
    , pcout(new fdostream(output_fd))
    , output(*pcout)
  {
    g_currentTerminal = this;
  }

  Terminal::~Terminal()
  {
    delete pcout;
  }

  Terminal& Terminal::Current()
  {
    if (g_currentTerminal == nullptr)
    {
      g_currentTerminal = new Terminal(STDIN_FILENO, STDOUT_FILENO);
    }
    return *g_currentTerminal;
  }

  Terminal Terminal::Create(int inputFd, int outputFd)
  {
    return Terminal(inputFd, outputFd);
  }

  bool Terminal::WaitForTerminalInput(int seconds, int microseconds)
  {
    timeval tv = {seconds, microseconds};
    fd_set fds;
    FD_ZERO(&fds);                                        // NOLINT
    FD_SET(m_input_fd, &fds);                             // NOLINT
    select(m_input_fd + 1, &fds, nullptr, nullptr, &tv);  // NOLINT
    return FD_ISSET(m_input_fd, &fds);                    // NOLINT
  }

  ssize_t Terminal::Read(char *buffer, size_t buffer_size, int timeoutMilliseconds)
  {
    if (timeoutMilliseconds > 0)
    {
      if (!WaitForTerminalInput(timeoutMilliseconds / 1000, (timeoutMilliseconds % 1000) * 1000))
      {
        return 0;
      }
    }
    return read(m_input_fd, buffer, buffer_size);  // NOLINT
  }

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

#if !defined(ESP32)
void Terminal::Uninstall()
{
  if (m_oldTerminalState == nullptr)
  {
    return;
  }
  tcsetattr(m_input_fd, TCSANOW, m_oldTerminalState.get());
}

void Terminal::Install()
{
  if (m_oldTerminalState || !isatty(m_input_fd))
  {
    return;
  }
  
  struct termios terminal;
  tcgetattr(m_input_fd, &terminal);

  m_oldTerminalState = std::make_unique<struct termios>(terminal);

  terminal.c_lflag &= ~ICANON;  // NOLINT Non canonique terminal.
  terminal.c_lflag &= ~ECHO;    // NOLINT Do not print after a key press.
  terminal.c_cc[VMIN] = 0;
  terminal.c_cc[VTIME] = 0;

  // auto oldf = fcntl(input_fd, F_GETFL, 0);
  // fcntl(input_fd, F_SETFL, oldf | O_NONBLOCK);
  // on_exit_functions.push([=] { fcntl(input_fd, F_GETFL, oldf); });

  tcsetattr(m_input_fd, TCSANOW, &terminal);
}
#endif // !defined(ESP32)

Dimensions Terminal::GetPsuedoTerminalSize() {
  if (g_cached_dimensions.dimx != 0 && g_cached_dimensions.dimy != 0) 
  {
    return g_cached_dimensions;  // already set
  }

  output << "\0337\033[r\033[999;999H\033[6n\0338";
  output.flush();
  
  char ch;
  std::string input;

  while ( Read(&ch, 1, 100) > 0 && ch != 'R') { // R terminates the response
    if ( EOF == ch) {
        break;
    }
    if ( isprint ( ch)) {
      input.push_back(ch);
    }
  }
 
  if (input.empty()) {
    return FallbackSize();
  }
  
  if (2 == sscanf(input.c_str(), "[%d;%d", &g_cached_dimensions.dimy, &g_cached_dimensions.dimx)) {
    // ensure we have at least 80 cols
    if (g_cached_dimensions.dimx < 80) {
      g_cached_dimensions.dimx = 80;
    }
    
    // ensure we have at least 24 rows
    if (g_cached_dimensions.dimy < 24) {
      g_cached_dimensions.dimy = 24;
    }

    g_cached_dimensions_time = std::chrono::steady_clock::now();

    return g_cached_dimensions;
  } else {
    return FallbackSize();
  }
}

void Terminal::ForceRecalculateSize()
{
  g_cached_dimensions = {0,0};
}

/// @brief Get the terminal size.
/// @return The terminal size.
/// @ingroup screen
Dimensions Terminal::Size() {
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
  #if defined(ESP32)
  constexpr bool IsPsuedoTerminal = true;
  #else 
  constexpr bool IsPsuedoTerminal = (m_output_fd != STDOUT_FILENO);
  #endif
  
  if (IsPsuedoTerminal) 
  {

    if (g_cached_dimensions.dimx != 0 && g_cached_dimensions.dimy != 0) 
    {
      return g_cached_dimensions;  // already set
    }

    // We have no cached size, so we need to calculate    

    if (auto screen = ScreenInteractive::Active())
    {
      // if we have a screen up and running, we need to restore settings before we can calculate the size
      screen->WithRestoredIO([&] {
        GetPsuedoTerminalSize(); // calculate the size and cache the result
      })();
    }
    
    return GetPsuedoTerminalSize();
  }

  if (!isatty(m_output_fd))  // NOLINT
  {
    return FallbackSize();
  }

  #if defined(ESP32)
    // Always the fallback size if we get here on ESP32
    return FallbackSize();
  #else
    winsize w{};
    const int status = ioctl(m_output_fd, TIOCGWINSZ, &w);  // NOLINT
    // The ioctl return value result should be checked. Some operating systems
    // don't support TIOCGWINSZ.
    if (w.ws_col == 0 || w.ws_row == 0 || status < 0) {
      return FallbackSize();
    }
    return Dimensions{w.ws_col, w.ws_row};
  #endif
#endif
}

/// @brief Override terminal size in case auto-detection fails
/// @param fallbackSize Terminal dimensions to fallback to
void SetFallbackSize(const Dimensions& fallbackSize) {
  FallbackSize() = fallbackSize;
}

/// @brief Get the color support of the terminal.
/// @ingroup screen
Terminal::Color Terminal::ColorSupport() {
  if (!g_cached) {
    g_cached = true;
    g_cached_supported_color = ComputeColorSupport();
  }
  return g_cached_supported_color;
}

/// @brief Override terminal color support in case auto-detection fails
/// @ingroup dom
void Terminal::SetColorSupport(Terminal::Color color) {
  g_cached = true;
  g_cached_supported_color = color;
}

}  // namespace ftxui
