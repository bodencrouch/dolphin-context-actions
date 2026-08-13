BINDIR ?= $(HOME)/.local/bin
SERVICEDIR ?= $(HOME)/.local/share/kio/servicemenus
CONFIGDIR ?= $(HOME)/.config/dolphin-context-actions
CONVERTERBIN ?= $(BINDIR)/dolphin-context-actions
REGISTRY ?= src/dolphin_context_actions/conversions.yaml
PLUGINBUILDDIR ?= build/kio-plugin
PLUGINPREFIX ?= /usr

.PHONY: all install uninstall pip-install install-menus install-menus-only plugin-build install-plugin doctor

all: install

# `pip install -e .` as root leaves a .pth file pointing back at this checkout
# owned by root but referencing a directory this user can still write to --
# see scripts/check-privileged-pth.py for the full explanation. Refuse rather
# than rely on catching it after the fact.
pip-install:
	@if [ "$$(id -u)" -eq 0 ]; then \
		echo "Do not run 'make pip-install'/'make install' as root (or via sudo/pkexec)." >&2; \
		echo "install-plugin below calls sudo itself for the one step that needs it." >&2; \
		exit 1; \
	fi
	pip install -e .

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
	python3 -m dolphin_context_actions.file_converter_menus --output-dir $(SERVICEDIR) --converter-bin $(CONVERTERBIN) --registry $(CONFIGDIR)/conversions.yaml
	rm -f $(SERVICEDIR)/dolphin-file-converter-*.desktop
	@echo "✓ Service menus installed. Restart Dolphin (killall dolphin) to reload."

plugin-build:
	cmake -S kio-plugin -B $(PLUGINBUILDDIR) -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_INSTALL_PREFIX=$(PLUGINPREFIX)
	cmake --build $(PLUGINBUILDDIR) --parallel

install-plugin: plugin-build
	sudo cmake --install $(PLUGINBUILDDIR)

install: pip-install install-menus install-plugin

install-menus-only:
	mkdir -p $(SERVICEDIR) $(CONFIGDIR)
	cp servicemenus/dolphin-context-actions.desktop $(SERVICEDIR)/
	chmod +x $(SERVICEDIR)/dolphin-context-actions.desktop
	cp servicemenus/dolphin-audio-converter.desktop $(SERVICEDIR)/
	chmod +x $(SERVICEDIR)/dolphin-audio-converter.desktop
	rm -f $(SERVICEDIR)/dolphin-link-extension.desktop
	rm -f $(SERVICEDIR)/dolphin-link-extension-bg.desktop
	@if [ ! -f "$(CONFIGDIR)/conversions.yaml" ]; then cp $(REGISTRY) $(CONFIGDIR)/conversions.yaml; fi
	PYTHONPATH=src python3 -m dolphin_context_actions.file_converter_menus --output-dir $(SERVICEDIR) --converter-bin $(CONVERTERBIN) --registry $(CONFIGDIR)/conversions.yaml
	rm -f $(SERVICEDIR)/dolphin-file-converter-*.desktop
	@echo "✓ Service menus installed."

uninstall:
	pip uninstall -y dolphin-context-actions 2>/dev/null || true
	rm -f $(SERVICEDIR)/dolphin-context-actions.desktop
	rm -f $(SERVICEDIR)/dolphin-audio-converter.desktop
	rm -f $(SERVICEDIR)/dolphin-link-extension.desktop
	rm -f $(SERVICEDIR)/dolphin-link-extension-bg.desktop
	rm -f $(SERVICEDIR)/dolphin-context-actions-convert-*.desktop
	rm -f $(SERVICEDIR)/dolphin-file-converter-*.desktop
	sudo rm -f $(PLUGINPREFIX)/lib64/qt6/plugins/kf6/kfileitemaction/dolphinlinkfileitemaction.so
	sudo rm -f /usr/libexec/kf6/kauth/linkhelper
	sudo rm -f /usr/share/polkit-1/actions/io.github.bodencrouch.linkhelper.policy
	sudo rm -f /usr/share/dbus-1/system-services/io.github.bodencrouch.linkhelper.service
	sudo rm -f /usr/share/dbus-1/system.d/io.github.bodencrouch.linkhelper.conf
	@echo "✓ Uninstalled."

reinstall: uninstall install
