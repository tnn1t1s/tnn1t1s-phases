# TNN1T1S Phases — VCV Rack plugin Makefile
#
# RACK_DIR must point at a Rack plugin SDK (or a Rack source checkout's
# build tree). Override it from the environment:
#
#     make RACK_DIR=/path/to/Rack-SDK
#
# By default it points at the rack-sdk vendored in the sibling vcv-rack repo,
# so the plugin builds out of the box on this machine.
#
# The default is home-anchored rather than repo-relative so it resolves
# identically from the main checkout and from any .worktrees/ checkout
# (a repo-relative default counts a different number of ..s per worktree
# depth). CI and the Library toolchain set RACK_DIR explicitly.
RACK_DIR ?= $(HOME)/Developer/vcv-rack/vendor/rack-sdk

FLAGS += -Ivendor/svghelper -Isrc

SOURCES += src/plugin.cpp
SOURCES += src/Phase.cpp
SOURCES += src/Expr.cpp

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

include $(RACK_DIR)/plugin.mk

RACK_PLUGINS := $(HOME)/Library/Application Support/Rack2/plugins-mac-arm64

# Fast in-place deploy for development (the SDK `install` target builds and
# installs a packaged .vcvplugin instead).
# The rm before cp matters: overwriting a dylib in place reuses the inode,
# and the kernel's cached code-signature for that inode goes stale -- Rack
# then dies in dlopen with SIGKILL "Code Signature Invalid". A fresh file
# plus an ad-hoc re-sign loads cleanly.
deploy: all
	mkdir -p "$(RACK_PLUGINS)/tnn1t1s-phases"
	rm -f "$(RACK_PLUGINS)/tnn1t1s-phases/plugin.dylib"
	cp plugin.dylib "$(RACK_PLUGINS)/tnn1t1s-phases/plugin.dylib"
	codesign --force -s - "$(RACK_PLUGINS)/tnn1t1s-phases/plugin.dylib"
	cp plugin.json  "$(RACK_PLUGINS)/tnn1t1s-phases/plugin.json"
	cp -r res/      "$(RACK_PLUGINS)/tnn1t1s-phases/res/"

.PHONY: deploy
