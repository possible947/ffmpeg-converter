ROOT := $(abspath ../..)
FPC ?= fpc
LAZBUILD ?= lazbuild

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
	test_analysis_parsers \
	test_default_output_preflight \
	test_output_preflight_rules \
	test_error_log_creation \
	test_unified_tool_resolver \
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
GUI_PROJECT ?= $(ROOT)/fpc/gui/form.lpi
LAZBUILD_FLAGS ?= -B

GUI_DEPS := \
	$(GUI_PROJECT) \
	$(GUI_MAIN) \
	$(ROOT)/fpc/gui/form.lfm \
	$(GUI_UNITS) \
	$(COMMON_UNITS) \
	$(CONVERTER_UNITS)

.PHONY: all cli lib tests apple-test gui gui-app clean

all: cli lib tests

cli: $(CLI_BIN)

lib: $(LIB_OUT)

tests: $(TEST_BINS)

apple-test: $(ROOT)/fpc/test/run_apple_m4v_test$(EXEEXT)

gui: $(GUI_BIN)

gui-app: $(GUI_BIN)
	@if [ "$(UNAME_S)" = "Darwin" ]; then \
		"$(ROOT)/fpc/build/package_macos_app.sh" "$(GUI_BIN)"; \
	else \
		echo "Skipping gui-app packaging on non-Darwin host."; \
	fi

$(GUI_BIN): $(GUI_DEPS)
	@command -v "$(LAZBUILD)" >/dev/null 2>&1 || { echo "ERROR: lazbuild not found in PATH."; exit 1; }
	@mkdir -p "$(ROOT)/fpc/gui" "$(UNIT_BASE)/gui"
	cd "$(ROOT)" && "$(LAZBUILD)" $(LAZBUILD_FLAGS) "$(GUI_PROJECT)"
	@test -f "$@" || { echo "ERROR: lazbuild finished but GUI binary not found at $@"; exit 1; }

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
	rm -rf "$(ROOT)/fpc/gui/form.app"
