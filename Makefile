RAYLIB_VERSION := 5.5
RAYLIB_DIR     := third_party/raylib-$(RAYLIB_VERSION)
RAYLIB_LIB     := $(RAYLIB_DIR)/src/libraylib.a

CXX      ?= g++

AI_DIR := src/ai_rs
AI_LIB := $(AI_DIR)/target/release/libai_brain.a

CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -I$(RAYLIB_DIR)/src -Ibuild
GFX_LIBS := -lGL -lm -lpthread -ldl -lrt -lX11 -lutil -lgcc_s
LDLIBS   := $(GFX_LIBS) -lcurl

SOURCES := src/app.cpp src/world.cpp src/mods.cpp src/lobby_net.cpp src/server_lookup.cpp src/updater.cpp
ifeq ($(OS),Windows_NT)
MKLOGO      := build/windows/mklogo.exe
else
MKLOGO      := build/mklogo
endif
LOGO_HEADER := build/logo_data.h
LOGO_PNG    := assets/logo.png
DESKTOP     := build/OverKillOpensource.desktop
OBJECTS := $(patsubst src/%.cpp,build/obj/%.o,$(SOURCES))
HEADERS := $(wildcard src/*.h)
TARGET  := build/OverKillOpensource

WIN_AVAILABLE := $(shell command -v x86_64-w64-mingw32-g++ 2>/dev/null)

.PHONY: all clean mods windows

all: $(TARGET) $(DESKTOP) mods $(if $(WIN_AVAILABLE),windows)
	@if [ -d music ]; then rm -rf build/music && cp -r music build/; fi

$(DESKTOP): $(TARGET)
	printf '%s\n' '[Desktop Entry]' 'Type=Application' 'Name=OverKill opensource' 'Exec=$(CURDIR)/$(TARGET)' 'Path=$(CURDIR)/build' 'Icon=$(CURDIR)/$(LOGO_PNG)' 'Terminal=false' 'Categories=Game;' > $@
	chmod +x $@

$(TARGET): $(OBJECTS) $(RAYLIB_LIB) $(AI_LIB)
	$(CXX) $(OBJECTS) -o $@ -Wl,--whole-archive $(RAYLIB_LIB) -Wl,--no-whole-archive $(AI_LIB) -rdynamic $(LDLIBS)

$(AI_LIB): $(wildcard $(AI_DIR)/src/*.rs) $(AI_DIR)/Cargo.toml
	cd $(AI_DIR) && cargo build --release

build/obj/%.o: src/%.cpp $(HEADERS) $(LOGO_HEADER) $(RAYLIB_LIB) | build/obj
	$(CXX) $(CXXFLAGS) -c $< -o $@

ifneq ($(OS),Windows_NT)
$(MKLOGO): tools/mklogo.cpp src/logo.h $(RAYLIB_LIB) | build/obj
	$(CXX) $(CXXFLAGS) tools/mklogo.cpp -o $@ $(RAYLIB_LIB) $(GFX_LIBS)
endif

$(LOGO_PNG) $(LOGO_HEADER): $(MKLOGO)
	mkdir -p assets
	$< $(LOGO_PNG) $(LOGO_HEADER)

build/obj:
	mkdir -p $@

$(RAYLIB_DIR)/src/raylib.h:
	mkdir -p third_party
	curl -L -o third_party/raylib-$(RAYLIB_VERSION).tar.gz \
		https://github.com/raysan5/raylib/archive/refs/tags/$(RAYLIB_VERSION).tar.gz
	tar -xzf third_party/raylib-$(RAYLIB_VERSION).tar.gz -C third_party

$(RAYLIB_LIB): $(RAYLIB_DIR)/src/raylib.h
	$(MAKE) -C $(RAYLIB_DIR)/src PLATFORM=PLATFORM_DESKTOP RAYLIB_LIBTYPE=STATIC

mods: $(RAYLIB_DIR)/src/raylib.h | build/mods
	cp src/modapi.h $(RAYLIB_DIR)/src/raylib.h modtutorial.txt build/mods/

build/mods:
	mkdir -p $@

WIN_DIR        := build/windows
WIN_RAYLIB_DIR := $(WIN_DIR)/raylib-$(RAYLIB_VERSION)
WIN_RAYLIB_LIB := $(WIN_DIR)/libraylib.a
WIN_CXX        := x86_64-w64-mingw32-g++
WIN_CC         := x86_64-w64-mingw32-gcc
WIN_AR         := x86_64-w64-mingw32-ar
WIN_AI_LIB     := $(AI_DIR)/target/x86_64-pc-windows-gnu/release/libai_brain.a
WIN_OBJ_DIR    := $(WIN_DIR)/obj
WIN_OBJECTS    := $(patsubst src/%.cpp,$(WIN_OBJ_DIR)/%.o,$(SOURCES))
WIN_TARGET     := $(WIN_DIR)/OverKillOpensource.exe
WIN_CXXFLAGS   := -std=c++17 -O2 -Wall -Wextra -I$(WIN_RAYLIB_DIR)/src -Ibuild
WIN_LDLIBS     := -lopengl32 -lgdi32 -lwinmm -lws2_32 -lpthread -luserenv -lbcrypt -ladvapi32 -lntdll -lwinhttp

# Windows cross-build (mingw-w64). Raylib is built from a copy so the Linux objects are untouched.
$(WIN_RAYLIB_LIB): $(RAYLIB_DIR)/src/raylib.h
	mkdir -p $(WIN_DIR)
	rm -rf $(WIN_RAYLIB_DIR)
	cp -r $(RAYLIB_DIR) $(WIN_RAYLIB_DIR)
	-$(MAKE) -C $(WIN_RAYLIB_DIR)/src clean
	$(MAKE) -C $(WIN_RAYLIB_DIR)/src PLATFORM=PLATFORM_DESKTOP PLATFORM_OS=WINDOWS RAYLIB_LIBTYPE=STATIC CC=$(WIN_CC) AR=$(WIN_AR)
	cp $(WIN_RAYLIB_DIR)/src/libraylib.a $@

$(WIN_AI_LIB): $(wildcard $(AI_DIR)/src/*.rs) $(AI_DIR)/Cargo.toml
	cd $(AI_DIR) && PATH="$$HOME/.cargo/bin:$$PATH" cargo build --release --target x86_64-pc-windows-gnu

$(WIN_OBJ_DIR)/%.o: src/%.cpp $(HEADERS) $(LOGO_HEADER) $(WIN_RAYLIB_LIB) | $(WIN_OBJ_DIR)
	$(WIN_CXX) $(WIN_CXXFLAGS) -c $< -o $@

$(WIN_OBJ_DIR):
	mkdir -p $@

$(WIN_TARGET): $(WIN_OBJECTS) $(WIN_RAYLIB_LIB) $(WIN_AI_LIB)
	$(WIN_CXX) $(WIN_OBJECTS) -o $@ -mwindows -static -static-libgcc -static-libstdc++ -Wl,--whole-archive $(WIN_RAYLIB_LIB) -Wl,--no-whole-archive $(WIN_AI_LIB) $(WIN_LDLIBS)

windows: $(WIN_TARGET)
	@if [ -d music ]; then rm -rf $(WIN_DIR)/music && cp -r music $(WIN_DIR)/; fi

ifeq ($(OS),Windows_NT)
$(MKLOGO): tools/mklogo.cpp src/logo.h $(WIN_RAYLIB_LIB)
	mkdir -p build/windows
	$(WIN_CXX) $(WIN_CXXFLAGS) tools/mklogo.cpp -o $@ $(WIN_RAYLIB_LIB) $(WIN_LDLIBS)
endif

clean:
	rm -rf build
