# Makefile for the SDL2 build of the Apple-1 Emulator.
#
# Build:    make
# Run:      ./apple1
# Clean:    make clean
#
# Requires: SDL2 dev headers + libraries.
#   Linux:   apt install libsdl2-dev
#   macOS:   brew install sdl2
#   Windows (MSYS2 MinGW): pacman -S mingw-w64-x86_64-SDL2

CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
SDL_CFLAGS  := $(shell sdl2-config --cflags)
SDL_LIBS    := $(shell sdl2-config --libs)

ROOT     := ../../..
SRC_CORE := $(ROOT)/src
SRC_SDL  := $(ROOT)/src/platform/sdl2

INCLUDES := -I$(SRC_CORE) -I$(SRC_SDL) $(SDL_CFLAGS)

CORE_SRC := \
    $(SRC_CORE)/bus.cpp \
    $(SRC_CORE)/cpu6502.cpp \
    $(SRC_CORE)/display_grid.cpp \
    $(SRC_CORE)/roms.cpp \
    $(SRC_CORE)/cassette.cpp \
    $(SRC_CORE)/fileio.cpp \
    $(SRC_CORE)/disasm.cpp \
    $(SRC_CORE)/debugger.cpp \
    $(SRC_CORE)/settings.cpp \
    $(SRC_CORE)/app.cpp

SDL_SRC := \
    $(SRC_SDL)/main.cpp \
    $(SRC_SDL)/sdl_app.cpp

ALL_SRC := $(CORE_SRC) $(SDL_SRC)
OBJ     := $(ALL_SRC:.cpp=.o)

apple1: $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ) $(SDL_LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJ) apple1 apple1.exe

.PHONY: clean
