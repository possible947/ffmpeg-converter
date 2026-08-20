ROOT := $(abspath ../..)
FPC ?= fpc
LAZBUILD ?= lazbuild

FPCFLAGS ?= -Mobjfpc -Sh -O2
UNIT_BASE := $(ROOT)/fpc/build/.units

ifeq ($(OS),Windows_NT)
EXEEXT := .exe
SHLIB_EXT := dll
LIB_OUT := $(ROOT)/fpc/converter/converter_pas.$(SHLIB_EXT)
# FPC on Windows is a native Win32/Win64 binary.  MSYS2 bash mangles paths
# when passing arguments to Windows executables:
#   - Unquoted backslash paths: bash strips '\' before each letter.
#   - Forward-slash drive paths (E:/...): MSYS2 rewrites them to C:\msys64\...
# Solution: embed double-quotes inside the Make variable so bash treats each
# -Fu argument as a double-quoted string.  Inside "...", bash preserves '\'
# before non-special characters.
ROOTW := $(shell cygpath -w "$(ROOT)")
COMMON_FU := "-Fu$(ROOTW)\fpc\converter" "-Fu$(ROOTW)\fpc\common" "-Fu$(ROOTW)\fpc\json" "-Fu$(ROOTW)\fpc\cli" "-Fu$(ROOTW)\fpc\platform"
UNIT_BASEW := $(ROOTW)\fpc\build\.units
CLI_LPR := $(ROOT)/fpc/cli/ffmpeg_converter_windows.lpr
else
UNAME_S := $(shell uname -s)
EXEEXT :=
COMMON_FU := -Fu$(ROOT)/fpc/converter -Fu$(ROOT)/fpc/common -Fu$(ROOT)/fpc/json -Fu$(ROOT)/fpc/cli -Fu$(ROOT)/fpc/platform
CLI_LPR := $(ROOT)/fpc/cli/ffmpeg_converter.lpr
ifeq ($(UNAME_S),Darwin)
SHLIB_EXT := dylib
LIB_OUT := $(ROOT)/fpc/converter/libconverter_pas.$(SHLIB_EXT)
else
SHLIB_EXT := so
LIB_OUT := $(ROOT)/fpc/converter/libconverter_pas.$(SHLIB_EXT)
endif
endif

CLI_BIN := $(ROOT)/fpc/bin/ffmpeg_converter$(EXEEXT)
GUI_BIN := $(ROOT)/fpc/bin/ffmpeg_converter_gui$(EXEEXT)
BIN_DIR := $(ROOT)/fpc/bin

COMMON_UNITS := \
	$(wildcard $(ROOT)/fpc/common/*.pas) \
	$(wildcard $(ROOT)/fpc/json/*.pas) \
	$(wildcard $(ROOT)/fpc/platform/*.pas)

CONVERTER_UNITS := \
	$(wildcard $(ROOT)/fpc/converter/*.pas)

CLI_UNITS := \
	$(wildcard $(ROOT)/fpc/cli/*.pas)

CLI_DEPS := \
	$(CLI_LPR) \
	$(COMMON_UNITS) \
	$(CONVERTER_UNITS) \
	$(CLI_UNITS)

LIB_DEPS := \
	$(ROOT)/fpc/converter/converter_pas.lpr \
	$(COMMON_UNITS) \
	$(CONVERTER_UNITS)

TEST_PROGRAMS := \
	test_cmd_builder \
	test_path_parse \
	test_cli_mode_matrix \
	test_analysis_parsers \
	test_default_output_preflight \
	test_output_preflight_rules \
	test_error_log_creation \
	test_unified_tool_resolver \
	run_apple_m4v_test

TEST_SOURCES := $(wildcard $(ROOT)/fpc/test/*.pas)
TEST_BINS := $(addprefix $(ROOT)/fpc/test/,$(addsuffix $(EXEEXT),$(TEST_PROGRAMS)))

TEST_SHARED_DEPS := \
	$(COMMON_UNITS) \
	$(CONVERTER_UNITS) \
	$(CLI_UNITS)

GUI_UNITS := \
	$(wildcard $(ROOT)/fpc/gui/*.pas)

GUI_MAIN ?= $(ROOT)/fpc/gui/main.lpr
GUI_MAIN_STEM := $(basename $(notdir $(GUI_MAIN)))
GUI_RES := $(ROOT)/fpc/gui/$(GUI_MAIN_STEM).res
GUI_PROJECT ?= $(ROOT)/fpc/gui/form.lpi
# Widgetset: Linux GUI targets LCL/GTK3 (GTK4 is not supported by LCL);
# Windows uses the native win32/win64 widgetset, so --ws must NOT be passed
# there (form.lpi is shared by both platforms and cannot pin a single one).
ifeq ($(OS),Windows_NT)
LAZBUILD_FLAGS ?= -B -q -q
else
LAZBUILD_FLAGS ?= -B -q -q --ws=gtk3
endif

GUI_DEPS := \
	$(GUI_PROJECT) \
	$(GUI_MAIN) \
	$(ROOT)/fpc/gui/form.lfm \
	$(GUI_UNITS) \
	$(COMMON_UNITS) \
	$(CONVERTER_UNITS)

# macOS Pascal support has been removed.  The primary macOS implementation
# is the native C/Cocoa GUI (src/gui_macos_native).  The FPC Makefile now
# only supports Linux and Windows builds.
ifeq ($(UNAME_S),Darwin)
.PHONY: all cli lib tests apple-test gui gui-app appimage clean

all cli lib tests apple-test gui gui-app appimage:
	@echo "ERROR: Pascal/FPC macOS build is no longer supported."
	@echo "       Use the native C/Cocoa GUI (src/gui_macos_native) instead."
	@exit 1

clean:
	rm -rf "$(UNIT_BASE)" "$(BIN_DIR)"

else

.PHONY: all cli lib tests apple-test gui gui-app appimage clean

all: cli lib tests copy-binaries

cli: $(CLI_BIN)

lib: $(LIB_OUT)

ifeq ($(strip $(TEST_SOURCES)),)
tests:
	@echo "No Pascal tests found under $(ROOT)/fpc/test; skipping tests target."
else
tests: $(TEST_BINS)
endif

apple-test: $(ROOT)/fpc/test/run_apple_m4v_test$(EXEEXT)

gui: $(GUI_BIN)

copy-binaries:
	@mkdir -p "$(BIN_DIR)"
	@if [ "$(CLI_BIN)" != "$(BIN_DIR)/ffmpeg_converter" ]; then \
		cp -f "$(CLI_BIN)" "$(BIN_DIR)/"; \
	fi
	@if [ "$(GUI_BIN)" != "$(BIN_DIR)/ffmpeg_converter_gui" ]; then \
		cp -f "$(GUI_BIN)" "$(BIN_DIR)/"; \
	fi
	@if [ -d "$(ROOT)/src/platform/linux/bin" ]; then \
		cp -f "$(ROOT)/src/platform/linux/bin/"* "$(BIN_DIR)/"; \
	fi
	@for tool in mkvmerge MP4Box; do \
		src=""; \
		if [ -x "$(ROOT)/src/platform/linux/bin/$$tool" ]; then \
			src="$(ROOT)/src/platform/linux/bin/$$tool"; \
		elif [ -x "$(ROOT)/build/bin/$$tool" ]; then \
			src="$(ROOT)/build/bin/$$tool"; \
		elif command -v "$$tool" >/dev/null 2>&1; then \
			src="$$(command -v "$$tool")"; \
		fi; \
		if [ -n "$$src" ]; then \
			cp -f "$$src" "$(BIN_DIR)/$$tool"; \
			chmod +x "$(BIN_DIR)/$$tool"; \
		fi; \
	done

gui-app: appimage

appimage: $(GUI_BIN)
	@mkdir -p "$(BIN_DIR)"
	bash "$(ROOT)/fpc/build/package_appimage.sh" "$(BIN_DIR)"

endif

$(GUI_BIN): $(GUI_DEPS)
	@command -v "$(LAZBUILD)" >/dev/null 2>&1 || { \
		echo "ERROR: lazbuild not found in PATH."; \
		echo "       Install Lazarus from https://www.lazarus-ide.org"; \
		exit 1; \
	}
	@mkdir -p "$(ROOT)/fpc/bin" "$(UNIT_BASE)/gui"
	@echo "==> Building Lazarus GUI: $(GUI_PROJECT)"
	@cd "$(ROOT)" && "$(LAZBUILD)" $(LAZBUILD_FLAGS) "$(GUI_PROJECT)" > "$(UNIT_BASE)/gui-build.log" 2>&1; status=$$?; cat "$(UNIT_BASE)/gui-build.log"; \
	if [ $$status -ne 0 ]; then \
		echo "ERROR: Lazarus GUI build failed."; \
		echo "       Full log: $(UNIT_BASE)/gui-build.log"; \
		echo "       ----- last lines -----"; \
		tail -n 12 "$(UNIT_BASE)/gui-build.log" | sed 's/^/       /'; \
		echo "       ----------------------"; \
		if grep -q 'cannot find -lgtk-3\|cannot find -lgdk-3' "$(UNIT_BASE)/gui-build.log"; then \
			echo "       The GTK3 development libraries are missing."; \
			echo "       On Debian/Ubuntu: sudo apt install libgtk-3-dev"; \
		elif grep -qE 'lclgtk3|LCLgtk3|widgetset' "$(UNIT_BASE)/gui-build.log"; then \
			echo "       The LCL GTK3 widgetset is missing or cannot be compiled."; \
			echo "       Note: Ubuntu/Debian apt has NO lcl-gtk3 package."; \
			echo "       Install Lazarus from https://www.lazarus-ide.org (includes GTK3)."; \
		else \
			echo "       See the log above for compiler errors in the project code."; \
		fi; \
		exit 1; \
	fi
	@test -f "$@" || { echo "ERROR: lazbuild finished but GUI binary not found at $@"; exit 1; }

$(CLI_BIN): $(CLI_DEPS)
	@mkdir -p "$(ROOT)/fpc/bin" "$(UNIT_BASE)/cli"
ifeq ($(OS),Windows_NT)
	$(FPC) $(FPCFLAGS) $(COMMON_FU) \
		-FU"$(UNIT_BASEW)\cli" \
		-FE"$(ROOTW)\fpc\bin" \
		-o"$(ROOTW)\fpc\bin\ffmpeg_converter$(EXEEXT)" \
		"$(ROOTW)\fpc\cli\ffmpeg_converter_windows.lpr"
else
	$(FPC) $(FPCFLAGS) $(COMMON_FU) \
		-FU"$(UNIT_BASE)/cli" \
		-FE"$(ROOT)/fpc/bin" \
		-o"$@" \
		"$<"
endif

$(LIB_OUT): $(LIB_DEPS)
	@mkdir -p "$(ROOT)/fpc/converter" "$(UNIT_BASE)/lib"
ifeq ($(OS),Windows_NT)
	$(FPC) $(FPCFLAGS) $(COMMON_FU) \
		-FU"$(UNIT_BASEW)\lib" \
		-FE"$(ROOTW)\fpc\converter" \
		-o"$(ROOTW)\fpc\converter\converter_pas.$(SHLIB_EXT)" \
		"$(ROOTW)\fpc\converter\converter_pas.lpr"
else
	$(FPC) $(FPCFLAGS) $(COMMON_FU) \
		-FU"$(UNIT_BASE)/lib" \
		-FE"$(ROOT)/fpc/converter" \
		-o"$@" \
		"$<"
endif

$(ROOT)/fpc/test/%$(EXEEXT): $(ROOT)/fpc/test/%.pas $(TEST_SHARED_DEPS)
	@mkdir -p "$(ROOT)/fpc/test" "$(UNIT_BASE)/tests/$*"
ifeq ($(OS),Windows_NT)
	$(FPC) $(FPCFLAGS) $(COMMON_FU) \
		-FU"$(UNIT_BASEW)\tests\$*" \
		-FE"$(ROOTW)\fpc\test" \
		-o"$(ROOTW)\fpc\test\$*$(EXEEXT)" \
		"$(ROOTW)\fpc\test\$*.pas"
else
	$(FPC) $(FPCFLAGS) $(COMMON_FU) \
		-FU"$(UNIT_BASE)/tests/$*" \
		-FE"$(ROOT)/fpc/test" \
		-o"$@" \
		"$<"
endif

clean:
	rm -rf "$(UNIT_BASE)"
	rm -f "$(CLI_BIN)" "$(LIB_OUT)" "$(GUI_BIN)" "$(GUI_RES)" $(TEST_BINS)
	rm -rf "$(ROOT)/fpc/gui/form.app"
	rm -rf "$(BIN_DIR)"
