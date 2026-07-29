BINDIR ?= $(HOME)/.local/bin
SERVICEDIR ?= $(HOME)/.local/share/kio/servicemenus
CONFIGDIR ?= $(HOME)/.config/dolphin-convert-actions
PLUGINBUILDDIR ?= build/kio-plugin
PLUGINPREFIX ?= /usr

.PHONY: all install uninstall pip-install install-menus install-menus-only plugin-build install-plugin

all: install

pip-install:
	pip install -e .

install-menus:
	mkdir -p $(SERVICEDIR) $(CONFIGDIR)
	cp servicemenus/dolphin-convert-actions.desktop $(SERVICEDIR)/
	chmod +x $(SERVICEDIR)/dolphin-convert-actions.desktop
	cp servicemenus/dolphin-audio-converter.desktop $(SERVICEDIR)/
	chmod +x $(SERVICEDIR)/dolphin-audio-converter.desktop
	rm -f $(SERVICEDIR)/dolphin-link-extension.desktop
	rm -f $(SERVICEDIR)/dolphin-link-extension-bg.desktop
	@echo "✓ Service menus installed. Restart Dolphin (killall dolphin) to reload."

plugin-build:
	cmake -S kio-plugin -B $(PLUGINBUILDDIR) -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_INSTALL_PREFIX=$(PLUGINPREFIX)
	cmake --build $(PLUGINBUILDDIR) --parallel

install-plugin: plugin-build
	sudo cmake --install $(PLUGINBUILDDIR)

install: pip-install install-menus install-plugin

install-menus-only:
	mkdir -p $(SERVICEDIR) $(CONFIGDIR)
	cp servicemenus/dolphin-convert-actions.desktop $(SERVICEDIR)/
	chmod +x $(SERVICEDIR)/dolphin-convert-actions.desktop
	cp servicemenus/dolphin-audio-converter.desktop $(SERVICEDIR)/
	chmod +x $(SERVICEDIR)/dolphin-audio-converter.desktop
	rm -f $(SERVICEDIR)/dolphin-link-extension.desktop
	rm -f $(SERVICEDIR)/dolphin-link-extension-bg.desktop
	@echo "✓ Service menus installed."

uninstall:
	pip uninstall -y dolphin-convert-actions 2>/dev/null || true
	rm -f $(SERVICEDIR)/dolphin-convert-actions.desktop
	rm -f $(SERVICEDIR)/dolphin-audio-converter.desktop
	rm -f $(SERVICEDIR)/dolphin-link-extension.desktop
	rm -f $(SERVICEDIR)/dolphin-link-extension-bg.desktop
	sudo rm -f $(PLUGINPREFIX)/lib64/qt6/plugins/kf6/kfileitemaction/dolphinlinkfileitemaction.so
	@echo "✓ Uninstalled."

reinstall: uninstall install
