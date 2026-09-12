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
EXEEXT :=
COMMON_FU := -Fu$(ROOT)/fpc/converter -Fu$(ROOT)/fpc/common -Fu$(ROOT)/fpc/json -Fu$(ROOT)/fpc/cli -Fu$(ROOT)/fpc/platform
CLI_LPR := $(ROOT)/fpc/cli/ffmpeg_converter_windows.lpr
SHLIB_EXT := dll
LIB_OUT := $(ROOT)/fpc/converter/converter_pas.$(SHLIB_EXT)
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
# Windows uses the native win32/win64 widgetset; --ws is intentionally omitted.
LAZBUILD_FLAGS ?= -B -q -q
GUI_WS := win32

GUI_DEPS := \
	$(GUI_PROJECT) \
	$(GUI_MAIN) \
	$(ROOT)/fpc/gui/form.lfm \
	$(GUI_UNITS) \
	$(COMMON_UNITS) \
	$(CONVERTER_UNITS)

# Pascal support is Windows-only. Linux and macOS use the C/CMake implementation.
ifneq ($(OS),Windows_NT)
.PHONY: all cli lib tests apple-test gui clean

all cli lib tests apple-test gui:
	@echo "ERROR: Pascal/FPC builds are supported on Windows only."
	@echo "       Use the C/CMake implementation for Linux and macOS."
	@exit 1

else

.PHONY: all cli lib tests apple-test gui clean

all: cli lib tests copy-binaries

cli: $(CLI_BIN) copy-binaries

lib: $(LIB_OUT)

ifeq ($(strip $(TEST_SOURCES)),)
tests:
	@echo "No Pascal tests found under $(ROOT)/fpc/test; skipping tests target."
else
tests: $(TEST_BINS)
endif

apple-test: $(ROOT)/fpc/test/run_apple_m4v_test$(EXEEXT)

gui: $(GUI_BIN) copy-binaries

stage-presets:
	@mkdir -p "$(BIN_DIR)"
	@test -f "$(ROOT)/presets.json" || { echo "ERROR: presets.json not found at $(ROOT)/presets.json"; exit 1; }
	@cp -f "$(ROOT)/presets.json" "$(BIN_DIR)/presets.json"

copy-binaries:
	@mkdir -p "$(BIN_DIR)"
	@if [ "$(CLI_BIN)" != "$(BIN_DIR)/ffmpeg_converter" ]; then \
		cp -f "$(CLI_BIN)" "$(BIN_DIR)/"; \
	fi
	@if [ "$(GUI_BIN)" != "$(BIN_DIR)/ffmpeg_converter_gui" ]; then \
		cp -f "$(GUI_BIN)" "$(BIN_DIR)/"; \
	fi
	@if [ -d "$(ROOT)/src/platform/windows/bin" ]; then \
		cp -f "$(ROOT)/src/platform/windows/bin/"* "$(BIN_DIR)/"; \
	fi
	@test -f "$(ROOT)/presets.json" || { echo "ERROR: presets.json not found at $(ROOT)/presets.json"; exit 1; }
	@cp -f "$(ROOT)/presets.json" "$(BIN_DIR)/presets.json"
	@for tool in mkvmerge MP4Box; do \
		src=""; \
		if [ -x "$(ROOT)/src/platform/windows/bin/$$tool" ]; then \
			src="$(ROOT)/src/platform/windows/bin/$$tool"; \
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

endif

$(GUI_BIN): $(GUI_DEPS)
	@command -v "$(LAZBUILD)" >/dev/null 2>&1 || { \
		echo "ERROR: lazbuild not found in PATH."; \
		echo "       Install Lazarus from https://www.lazarus-ide.org"; \
		exit 1; \
	}
	@mkdir -p "$(ROOT)/fpc/bin" "$(UNIT_BASE)/gui"
	@echo "==> Building Lazarus GUI: $(GUI_PROJECT) (widgetset: $(GUI_WS))"
	@cd "$(ROOT)" && "$(LAZBUILD)" $(LAZBUILD_FLAGS) "$(GUI_PROJECT)" > "$(UNIT_BASE)/gui-build.log" 2>&1; status=$$?; cat "$(UNIT_BASE)/gui-build.log"; \
	if [ $$status -ne 0 ]; then \
		echo "ERROR: Lazarus GUI build failed."; \
		echo "       Full log: $(UNIT_BASE)/gui-build.log"; \
		echo "       ----- last lines -----"; \
		tail -n 12 "$(UNIT_BASE)/gui-build.log" | sed 's/^/       /'; \
		echo "       ----------------------"; \
		echo "       See the log above for compiler errors in the project code."; \
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
