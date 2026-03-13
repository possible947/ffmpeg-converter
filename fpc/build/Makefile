ROOT := $(abspath ../..)
FPC ?= fpc

FPCFLAGS ?= -Mobjfpc -Sh -O2
COMMON_FU := -Fu$(ROOT)/fpc/converter -Fu$(ROOT)/fpc/common -Fu$(ROOT)/fpc/json -Fu$(ROOT)/fpc/cli
UNIT_BASE := $(ROOT)/fpc/build/.units

ifeq ($(OS),Windows_NT)
EXEEXT := .exe
SHLIB_EXT := dll
LIB_OUT := $(ROOT)/fpc/converter/converter_pas.$(SHLIB_EXT)
else
UNAME_S := $(shell uname -s)
EXEEXT :=
ifeq ($(UNAME_S),Darwin)
SHLIB_EXT := dylib
LIB_OUT := $(ROOT)/fpc/converter/libconverter_pas.$(SHLIB_EXT)
else
SHLIB_EXT := so
LIB_OUT := $(ROOT)/fpc/converter/libconverter_pas.$(SHLIB_EXT)
endif
endif

CLI_BIN := $(ROOT)/fpc/cli/ffmpeg_converter$(EXEEXT)
GUI_BIN := $(ROOT)/fpc/gui/ffmpeg_converter_gui$(EXEEXT)

COMMON_UNITS := \
	$(wildcard $(ROOT)/fpc/common/*.pas) \
	$(wildcard $(ROOT)/fpc/json/*.pas)

CONVERTER_UNITS := \
	$(wildcard $(ROOT)/fpc/converter/*.pas)

CLI_UNITS := \
	$(wildcard $(ROOT)/fpc/cli/*.pas)

CLI_DEPS := \
	$(ROOT)/fpc/cli/ffmpeg_converter.lpr \
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
	run_apple_m4v_test

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

# GUI rule supports plain FPC compilation (no Lazarus IDE), but still needs LCL units installed.
FPC_TARGET_CPU ?= $(shell $(FPC) -iTP 2>/dev/null || echo x86_64)
FPC_TARGET_OS ?= $(shell $(FPC) -iTO 2>/dev/null || echo linux)
LAZARUS_PREFIX ?= /usr/lib/lazarus
LAZARUS_VERSION ?= $(shell ls -1 "$(LAZARUS_PREFIX)" 2>/dev/null | sort -V | tail -n 1)
LAZARUS_DIR ?= $(LAZARUS_PREFIX)/$(LAZARUS_VERSION)
LCL_WIDGETSET ?= gtk2
LCL_UNITS_DIR ?= $(LAZARUS_DIR)/lcl/units/$(FPC_TARGET_CPU)-$(FPC_TARGET_OS)
WIDGETSET_UNITS_DIR ?= $(LCL_UNITS_DIR)/$(LCL_WIDGETSET)
LAZUTIL_UNITS_DIR ?= $(LAZARUS_DIR)/components/lazutils/lib/$(FPC_TARGET_CPU)-$(FPC_TARGET_OS)
GUI_EXTRA_FU ?=
GUI_FPCFLAGS ?= $(FPCFLAGS) -B

GUI_DEPS := \
	$(GUI_MAIN) \
	$(ROOT)/fpc/gui/form.lfm \
	$(GUI_UNITS) \
	$(COMMON_UNITS) \
	$(CONVERTER_UNITS)

.PHONY: all cli lib tests apple-test gui clean

all: cli lib tests

cli: $(CLI_BIN)

lib: $(LIB_OUT)

tests: $(TEST_BINS)

apple-test: $(ROOT)/fpc/test/run_apple_m4v_test$(EXEEXT)

gui: $(GUI_BIN)

$(GUI_BIN): $(GUI_DEPS)
	@test -n "$(LAZARUS_VERSION)" -o "$(LAZARUS_DIR)" != "$(LAZARUS_PREFIX)/" || { echo "ERROR: could not auto-detect LAZARUS_VERSION under $(LAZARUS_PREFIX)."; echo "Set LAZARUS_VERSION or LAZARUS_DIR explicitly."; exit 1; }
	@test -d "$(LCL_UNITS_DIR)" || { echo "ERROR: LCL units not found: $(LCL_UNITS_DIR)"; echo "Set LAZARUS_DIR/LAZARUS_VERSION/LCL_UNITS_DIR as needed."; exit 1; }
	@test -d "$(WIDGETSET_UNITS_DIR)" || { echo "ERROR: widgetset units not found: $(WIDGETSET_UNITS_DIR)"; echo "Set LCL_WIDGETSET (e.g. gtk2, gtk3, qt5) or override WIDGETSET_UNITS_DIR."; exit 1; }
	@test -d "$(LAZUTIL_UNITS_DIR)" || { echo "ERROR: lazutils units not found: $(LAZUTIL_UNITS_DIR)"; echo "Override LAZUTIL_UNITS_DIR if your distro layout differs."; exit 1; }
	@if [ ! -f "$(GUI_RES)" ]; then \
		if command -v lazres >/dev/null 2>&1; then \
			lazres "$(GUI_RES)" "$(ROOT)/fpc/gui/form.lfm"; \
		elif [ -x "$(LAZARUS_DIR)/lazres" ]; then \
			"$(LAZARUS_DIR)/lazres" "$(GUI_RES)" "$(ROOT)/fpc/gui/form.lfm"; \
		else \
			echo "ERROR: lazres not found to generate $(GUI_RES)."; \
			echo "Install Lazarus tools or generate form.res manually."; \
			exit 1; \
		fi; \
	fi
	@mkdir -p "$(ROOT)/fpc/gui" "$(UNIT_BASE)/gui"
	$(FPC) $(GUI_FPCFLAGS) \
		-Fu"$(ROOT)/fpc/gui" \
		$(COMMON_FU) \
		-Fu"$(LCL_UNITS_DIR)" \
		-Fu"$(WIDGETSET_UNITS_DIR)" \
		-Fu"$(LAZUTIL_UNITS_DIR)" \
		$(GUI_EXTRA_FU) \
		-FU"$(UNIT_BASE)/gui" \
		-FE"$(ROOT)/fpc/gui" \
		-o"$@" \
		"$(GUI_MAIN)"

$(CLI_BIN): $(CLI_DEPS)
	@mkdir -p "$(ROOT)/fpc/cli" "$(UNIT_BASE)/cli"
	$(FPC) $(FPCFLAGS) $(COMMON_FU) \
		-FU"$(UNIT_BASE)/cli" \
		-FE"$(ROOT)/fpc/cli" \
		-o"$@" \
		"$<"

$(LIB_OUT): $(LIB_DEPS)
	@mkdir -p "$(ROOT)/fpc/converter" "$(UNIT_BASE)/lib"
	$(FPC) $(FPCFLAGS) $(COMMON_FU) \
		-FU"$(UNIT_BASE)/lib" \
		-FE"$(ROOT)/fpc/converter" \
		-o"$@" \
		"$<"

$(ROOT)/fpc/test/%$(EXEEXT): $(ROOT)/fpc/test/%.pas $(TEST_SHARED_DEPS)
	@mkdir -p "$(ROOT)/fpc/test" "$(UNIT_BASE)/tests/$*"
	$(FPC) $(FPCFLAGS) $(COMMON_FU) \
		-FU"$(UNIT_BASE)/tests/$*" \
		-FE"$(ROOT)/fpc/test" \
		-o"$@" \
		"$<"

clean:
	rm -rf "$(UNIT_BASE)"
	rm -f "$(CLI_BIN)" "$(LIB_OUT)" "$(GUI_BIN)" "$(GUI_RES)" $(TEST_BINS)
