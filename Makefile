# Scrybe — convenience wrapper around CMake and the setup script.
PREFIX ?= $(HOME)/.local
BUILD  ?= build

.PHONY: all build run configure install setup uninstall clean

all: build

configure:
	cmake -S . -B $(BUILD) -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$(PREFIX)

build: configure
	cmake --build $(BUILD) -j

run: build
	$(BUILD)/bin/scrybe

# Full first-time install (deps, models, desktop entry, hotkey, autostart).
setup:
	./build-and-setup.sh

# Install just the built binary + desktop launcher + icon (deps already present).
install: build
	install -Dm755 $(BUILD)/bin/scrybe $(PREFIX)/bin/scrybe
	install -Dm644 packaging/scrybe.svg $(PREFIX)/share/icons/hicolor/scalable/apps/scrybe.svg
	sed 's|^Exec=scrybe|Exec=$(PREFIX)/bin/scrybe|' packaging/scrybe.desktop \
		> $(PREFIX)/share/applications/scrybe.desktop
	@update-desktop-database $(PREFIX)/share/applications 2>/dev/null || true

uninstall:
	./scripts/uninstall.sh

clean:
	rm -rf $(BUILD)
