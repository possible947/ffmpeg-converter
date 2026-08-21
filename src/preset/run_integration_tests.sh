#!/bin/bash

# Integration test: Verify preset loader loads actual presets.json

set -e

TEST_DIR="$(dirname "${BASH_SOURCE[0]}")"
PROJECT_ROOT="$(cd "$TEST_DIR/../.." && pwd)"
BINARY="$PROJECT_ROOT/build/src/preset/test_preset_loader"
PRESETS_FILE="$PROJECT_ROOT/bin/presets.json"

if [ ! -f "$BINARY" ]; then
    echo "❌ Test binary not found: $BINARY"
    exit 1
fi

if [ ! -f "$PRESETS_FILE" ]; then
    echo "❌ Presets file not found: $PRESETS_FILE"
    exit 1
fi

echo "╔════════════════════════════════════════════════════════════╗"
echo "║          INTEGRATION TEST: Load presets.json               ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""
echo "Binary:       $BINARY"
echo "Presets:      $PRESETS_FILE"
echo ""

# Copy presets.json to executable directory for search path testing
cp "$PRESETS_FILE" "$PROJECT_ROOT/build/bin/presets.json"
echo "✓ Copied presets.json to build/bin/ for search path testing"

# Run test
echo ""
echo "Running tests..."
"$BINARY"

TEST_RESULT=$?

echo ""
echo "╔════════════════════════════════════════════════════════════╗"
echo "║                  INTEGRATION TEST RESULT                   ║"
echo "╚════════════════════════════════════════════════════════════╝"

if [ $TEST_RESULT -eq 0 ]; then
    echo "✓ All tests passed!"
    exit 0
else
    echo "❌ Tests failed!"
    exit 1
fi
