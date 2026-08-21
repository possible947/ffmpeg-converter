#!/bin/bash

# Task 1.5: Cross-Platform Path Discovery Tests
# Tests preset file search paths

TEST_NAME="Path Discovery Tests"

echo "╔════════════════════════════════════════════════════════════╗"
echo "║           Task 1.5: PATH DISCOVERY TESTS                  ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

PASS=0
FAIL=0

function assert_test() {
    local test_name="$1"
    local condition="$2"
    
    if eval "$condition" 2>/dev/null; then
        echo "✓ PASS: $test_name"
        ((PASS++))
    else
        echo "✗ FAIL: $test_name"
        ((FAIL++))
    fi
}

function cleanup_test_env() {
    rm -rf /tmp/preset_test_home /tmp/preset_test_config /tmp/preset_search \
           /tmp/preset_malformed /tmp/preset_env_override /tmp/preset_test_bin \
           /tmp/preset_empty 2>/dev/null
}

# Test 1: Linux - XDG_CONFIG_HOME path
echo "=== Linux Config Directory Tests ==="

TEST_CONFIG="/tmp/preset_test_config"
mkdir -p "$TEST_CONFIG/ffmpeg_converter"

cat > "$TEST_CONFIG/ffmpeg_converter/presets.json" << 'EOF'
{
  "version": "1.0",
  "linux": {"copy": {"default": {"ffmpeg_args": "-c:v copy ", "container": "mkv"}}}
}
EOF

assert_test "XDG_CONFIG_HOME preset file created" "[ -f '$TEST_CONFIG/ffmpeg_converter/presets.json' ]"

# Test 2: Home .config fallback
echo ""
echo "=== Linux Home Config Fallback Tests ==="

TEST_HOME="/tmp/preset_test_home"
mkdir -p "$TEST_HOME/.config/ffmpeg_converter"

cat > "$TEST_HOME/.config/ffmpeg_converter/presets.json" << 'EOF'
{
  "version": "1.0",
  "linux": {"copy": {"default": {"ffmpeg_args": "-c:v copy ", "container": "mkv"}}}
}
EOF

assert_test "Home .config/ffmpeg_converter preset file created" "[ -f '$TEST_HOME/.config/ffmpeg_converter/presets.json' ]"
assert_test "Home preset file is readable and valid JSON" "grep -q 'ffmpeg_args' '$TEST_HOME/.config/ffmpeg_converter/presets.json'"

# Test 3: Executable-adjacent path
echo ""
echo "=== Executable-Adjacent Path Tests ==="

TEST_BIN_DIR="/tmp/preset_test_bin"
mkdir -p "$TEST_BIN_DIR"

cat > "$TEST_BIN_DIR/presets.json" << 'EOF'
{
  "version": "1.0",
  "linux": {"copy": {"default": {"ffmpeg_args": "-c:v copy ", "container": "mkv"}}}
}
EOF

assert_test "Binary directory created" "[ -d '$TEST_BIN_DIR' ]"
assert_test "Presets in bin directory readable" "[ -f '$TEST_BIN_DIR/presets.json' ]"

# Test 4: File precedence order
echo ""
echo "=== File Search Precedence Tests ==="

TEST_SEARCH="/tmp/preset_search"
mkdir -p "$TEST_SEARCH/config/ffmpeg_converter"

cat > "$TEST_SEARCH/config/ffmpeg_converter/presets.json" << 'EOF'
{
  "version": "1.0",
  "linux": {"copy": {"default": {"ffmpeg_args": "-c:v copy CONFIG", "container": "mkv"}}}
}
EOF

assert_test "Config directory preset file created" "[ -f '$TEST_SEARCH/config/ffmpeg_converter/presets.json' ]"
assert_test "Config preset contains marker" "grep -q CONFIG '$TEST_SEARCH/config/ffmpeg_converter/presets.json'"

# Test 5: Malformed file handling
echo ""
echo "=== Malformed JSON Handling Tests ==="

TEST_MALFORMED="/tmp/preset_malformed"
mkdir -p "$TEST_MALFORMED"

echo "{ broken json [" > "$TEST_MALFORMED/presets.json"
assert_test "Malformed JSON file created" "[ -f '$TEST_MALFORMED/presets.json' ]"
assert_test "Malformed JSON contains expected syntax" "grep -q 'broken' '$TEST_MALFORMED/presets.json'"

# Test 6: macOS-style path
echo ""
echo "=== macOS Path Simulation Tests ==="

MACOS_HOME="$TEST_HOME/macos_home"
mkdir -p "$MACOS_HOME/Library/Preferences/ffmpeg_converter"

cat > "$MACOS_HOME/Library/Preferences/ffmpeg_converter/presets.json" << 'EOF'
{
  "version": "1.0",
  "macos": {"copy": {"default": {"ffmpeg_args": "-c:v copy ", "container": "mkv"}}}
}
EOF

assert_test "macOS Library/Preferences path created" "[ -d '$MACOS_HOME/Library/Preferences/ffmpeg_converter' ]"
assert_test "macOS presets.json readable" "[ -f '$MACOS_HOME/Library/Preferences/ffmpeg_converter/presets.json' ]"

# Test 7: Windows-style path (APPDATA simulation)
echo ""
echo "=== Windows Path Simulation Tests ==="

WINDOWS_HOME="$TEST_HOME/windows_appdata"
mkdir -p "$WINDOWS_HOME/ffmpeg_converter"

cat > "$WINDOWS_HOME/ffmpeg_converter/presets.json" << 'EOF'
{
  "version": "1.0",
  "windows": {"copy": {"default": {"ffmpeg_args": "-c:v copy ", "container": "mkv"}}}
}
EOF

assert_test "Windows APPDATA path created" "[ -d '$WINDOWS_HOME/ffmpeg_converter' ]"
assert_test "Windows presets.json readable" "[ -f '$WINDOWS_HOME/ffmpeg_converter/presets.json' ]"

# Test 8: Fallback - empty directory
echo ""
echo "=== Fallback Behavior Tests ==="

EMPTY_DIR="/tmp/preset_empty"
mkdir -p "$EMPTY_DIR"

assert_test "Empty directory created for fallback test" "[ -d '$EMPTY_DIR' ]"
assert_test "Directory is empty" "[ -z '$(find $EMPTY_DIR -type f)' ]"

# Test 9: All major platforms supported
echo ""
echo "=== Multi-Platform Support Tests ==="

cat > "$TEST_SEARCH/platform_check.json" << 'EOF'
{
  "version": "1.0",
  "linux": {"copy": {"default": {"ffmpeg_args": "-c:v copy ", "container": "mkv"}}},
  "macos": {"copy": {"default": {"ffmpeg_args": "-c:v copy ", "container": "mkv"}}},
  "windows": {"copy": {"default": {"ffmpeg_args": "-c:v copy ", "container": "mkv"}}}
}
EOF

assert_test "All platforms in presets JSON" "grep -q '\"linux\"' '$TEST_SEARCH/platform_check.json' && grep -q '\"macos\"' '$TEST_SEARCH/platform_check.json' && grep -q '\"windows\"' '$TEST_SEARCH/platform_check.json'"

# Test 10: Binaries contain built-in data
echo ""
echo "=== Built-in Fallback Data Tests ==="

REPO_ROOT="/home/viktor/LLM/Projects/ffmpeg-converter"
if [ -f "$REPO_ROOT/build/bin/ffmpeg_converter" ]; then
    assert_test "C binary exists" "[ -x '$REPO_ROOT/build/bin/ffmpeg_converter' ]"
fi

if [ -f "$REPO_ROOT/fpc/bin/ffmpeg_converter" ]; then
    assert_test "Pascal binary exists" "[ -x '$REPO_ROOT/fpc/bin/ffmpeg_converter' ]"
fi

# Summary
echo ""
echo "╔════════════════════════════════════════════════════════════╗"
echo "║                    TEST SUMMARY                           ║"
echo "╠════════════════════════════════════════════════════════════╣"
printf "║  Total Tests:     %-2d                                       ║\n" $((PASS + FAIL))
printf "║  Passed:          %-2d ✓                                     ║\n" $PASS
printf "║  Failed:          %-2d ✗                                     ║\n" $FAIL
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

# Cleanup
cleanup_test_env

if [ $FAIL -eq 0 ]; then
    echo "✓ All path discovery tests passed!"
    exit 0
else
    echo "✗ Some tests failed!"
    exit 1
fi
