# BigBubbleMuff for Haiku — thin wrapper over CMake + ninja, plus the sync step.
#
# The Haiku machine is not a git checkout: develop on the dev host, rsync the
# tree across, build over ssh. Every target below works from either side —
# `sync` and the `remote-*` targets run from the dev host, the rest are what
# they invoke on the target.
#
# Typical loop from the dev host:
#   make -C haiku sync remote-build remote-install remote-validate

BUILD_DIR ?= build
BUILD_TYPE ?= Release
VST3_INSTALL_DIR ?= $(HOME)/config/non-packaged/add-ons/media/VST3

# Dev-host side: where the tree lands on the target, and how to get there.
TARGET ?= haikulaptop
REMOTE_DIR ?= bigbubblemuff
REPO_DIR := $(CURDIR)/..

all: build

# ---- on the Haiku target ---------------------------------------------------

configure:
	cmake -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

$(BUILD_DIR)/build.ninja:
	cmake -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

build: $(BUILD_DIR)/build.ninja
	ninja -C $(BUILD_DIR)

install: build
	mkdir -p $(VST3_INSTALL_DIR)
	@for bundle in $$(find $(BUILD_DIR) -type d -name "*.vst3" -not -path "*/CMakeFiles/*"); do \
		echo "install $$bundle -> $(VST3_INSTALL_DIR)/$$(basename $$bundle)"; \
		rm -rf "$(VST3_INSTALL_DIR)/$$(basename $$bundle)"; \
		cp -r "$$bundle" "$(VST3_INSTALL_DIR)/"; \
	done

validate:
	@bundle="$(VST3_INSTALL_DIR)/BigBubbleMuff.vst3"; \
	[ -d "$$bundle" ] || { echo "not installed: $$bundle"; exit 2; }; \
	$(BUILD_DIR)/bin/$(BUILD_TYPE)/validator "$$bundle"

clean:
	rm -rf $(BUILD_DIR)

# ---- from the dev host -----------------------------------------------------

# Only what the Haiku build needs: this directory, the shared art, and the
# schematic notes the DSP cites. The Linux tree and build dirs stay behind.
sync:
	ssh $(TARGET) 'mkdir -p $(REMOTE_DIR)/haiku $(REMOTE_DIR)/gui'
	rsync -a --delete --exclude build --exclude 'build-*' \
		$(REPO_DIR)/haiku/ $(TARGET):$(REMOTE_DIR)/haiku/
	rsync -a --delete $(REPO_DIR)/gui/ $(TARGET):$(REMOTE_DIR)/gui/

remote-configure:
	ssh $(TARGET) 'cd $(REMOTE_DIR)/haiku && $(MAKE) configure'

remote-build:
	ssh $(TARGET) 'cd $(REMOTE_DIR)/haiku && $(MAKE) build'

remote-install:
	ssh $(TARGET) 'cd $(REMOTE_DIR)/haiku && $(MAKE) install'

remote-validate:
	ssh $(TARGET) 'cd $(REMOTE_DIR)/haiku && $(MAKE) validate'

.PHONY: all configure build install validate clean \
	sync remote-configure remote-build remote-install remote-validate
