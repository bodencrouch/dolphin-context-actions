BINDIR ?= $(HOME)/.local/bin
SERVICEDIR ?= $(HOME)/.local/share/kio/servicemenus
CONFIGDIR ?= $(HOME)/.config/dolphin-context-actions
CONVERTERBIN ?= $(BINDIR)/dolphin-context-actions
REGISTRY ?= assets/conversions.yaml
PLUGINBUILDDIR ?= build/kio-plugin
PLUGINPREFIX ?= /usr

.PHONY: all install uninstall cargo-install install-menus install-menus-only plugin-build install-plugin doctor

all: install

# Installing as root writes into root's ~/.local while the plugin step still
# needs sudo on its own. Refuse rather than mix the two.
cargo-install:
	@if [ "$$(id -u)" -eq 0 ]; then \
		echo "Do not run 'make install' as root (or via sudo/pkexec)." >&2; \
		echo "install-plugin below calls sudo itself for the one step that needs it." >&2; \
		exit 1; \
	fi
	cargo install --path . --root $(HOME)/.local --force --locked || cargo install --path . --root $(HOME)/.local --force

doctor:
	python3 scripts/check-privileged-pth.py

install-menus:
	mkdir -p $(SERVICEDIR) $(CONFIGDIR)
	cp servicemenus/dolphin-context-actions.desktop $(SERVICEDIR)/
	chmod +x $(SERVICEDIR)/dolphin-context-actions.desktop
	cp servicemenus/dolphin-audio-converter.desktop $(SERVICEDIR)/
	chmod +x $(SERVICEDIR)/dolphin-audio-converter.desktop
	rm -f $(SERVICEDIR)/dolphin-link-extension.desktop
	rm -f $(SERVICEDIR)/dolphin-link-extension-bg.desktop
	@if [ ! -f "$(CONFIGDIR)/conversions.yaml" ]; then cp $(REGISTRY) $(CONFIGDIR)/conversions.yaml; fi
	$(CONVERTERBIN) --generate-menus --output-dir $(SERVICEDIR) --converter-bin $(CONVERTERBIN) --registry $(CONFIGDIR)/conversions.yaml
	rm -f $(SERVICEDIR)/dolphin-file-converter-*.desktop

plugin-build:
	cmake -S kio-plugin -B $(PLUGINBUILDDIR) -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_INSTALL_PREFIX=$(PLUGINPREFIX)
	cmake --build $(PLUGINBUILDDIR) --parallel

install-plugin: plugin-build
	sudo cmake --install $(PLUGINBUILDDIR)

install: cargo-install install-menus install-plugin

install-menus-only:
	mkdir -p $(SERVICEDIR) $(CONFIGDIR)
	cp servicemenus/dolphin-context-actions.desktop $(SERVICEDIR)/
	chmod +x $(SERVICEDIR)/dolphin-context-actions.desktop
	cp servicemenus/dolphin-audio-converter.desktop $(SERVICEDIR)/
	chmod +x $(SERVICEDIR)/dolphin-audio-converter.desktop
	rm -f $(SERVICEDIR)/dolphin-link-extension.desktop
	rm -f $(SERVICEDIR)/dolphin-link-extension-bg.desktop
	@if [ ! -f "$(CONFIGDIR)/conversions.yaml" ]; then cp $(REGISTRY) $(CONFIGDIR)/conversions.yaml; fi
	cargo run --quiet -- --generate-menus --output-dir $(SERVICEDIR) --converter-bin $(CONVERTERBIN) --registry $(CONFIGDIR)/conversions.yaml
	rm -f $(SERVICEDIR)/dolphin-file-converter-*.desktop

uninstall:
	rm -f $(BINDIR)/dolphin-context-actions
	rm -f $(BINDIR)/dolphin-context-actions-generate-menus
	rm -f $(SERVICEDIR)/dolphin-context-actions.desktop
	rm -f $(SERVICEDIR)/dolphin-audio-converter.desktop
	rm -f $(SERVICEDIR)/dolphin-link-extension.desktop
	rm -f $(SERVICEDIR)/dolphin-link-extension-bg.desktop
	rm -f $(SERVICEDIR)/dolphin-context-actions-convert-*.desktop
	rm -f $(SERVICEDIR)/dolphin-file-converter-*.desktop
	sudo rm -f $(PLUGINPREFIX)/lib64/qt6/plugins/kf6/kfileitemaction/dolphinlinkfileitemaction.so
	sudo rm -f $(PLUGINPREFIX)/lib64/qt6/plugins/kf6/kfileitemaction/dolphinarkfileitemaction.so
	sudo rm -f /usr/libexec/kf6/kauth/linkhelper
	sudo rm -f /usr/share/polkit-1/actions/io.github.bodencrouch.linkhelper.policy
	sudo rm -f /usr/share/dbus-1/system-services/io.github.bodencrouch.linkhelper.service
	sudo rm -f /usr/share/dbus-1/system.d/io.github.bodencrouch.linkhelper.conf

reinstall: uninstall install
