BINDIR ?= $(HOME)/.local/bin
SERVICEDIR ?= $(HOME)/.local/share/kio/servicemenus
CONFIGDIR ?= $(HOME)/.config/dolphin-convert-actions

.PHONY: all install uninstall link-dev

all: pip-install install-menus

pip-install:
	pip install -e .

install-menus:
	mkdir -p $(SERVICEDIR) $(CONFIGDIR)
	cp servicemenus/dolphin-convert-actions.desktop $(SERVICEDIR)/
	cp servicemenus/dolphin-audio-converter.desktop $(SERVICEDIR)/
	cp servicemenus/dolphin-link-extension.desktop $(SERVICEDIR)/
	cp servicemenus/dolphin-link-extension-bg.desktop $(SERVICEDIR)/
	@echo "✓ Service menus installed. Restart Dolphin (killall dolphin) to reload."

install: pip-install install-menus

install-menus-only:
	mkdir -p $(SERVICEDIR) $(CONFIGDIR)
	cp servicemenus/dolphin-convert-actions.desktop $(SERVICEDIR)/
	cp servicemenus/dolphin-audio-converter.desktop $(SERVICEDIR)/
	cp servicemenus/dolphin-link-extension.desktop $(SERVICEDIR)/
	cp servicemenus/dolphin-link-extension-bg.desktop $(SERVICEDIR)/
	@echo "✓ Service menus installed."

uninstall:
	pip uninstall -y dolphin-convert-actions 2>/dev/null || true
	rm -f $(SERVICEDIR)/dolphin-convert-actions.desktop
	rm -f $(SERVICEDIR)/dolphin-audio-converter.desktop
	rm -f $(SERVICEDIR)/dolphin-link-extension.desktop
	rm -f $(SERVICEDIR)/dolphin-link-extension-bg.desktop
	@echo "✓ Uninstalled."

reinstall: uninstall install
