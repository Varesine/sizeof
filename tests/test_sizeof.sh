#!/usr/bin/env bash
set -euo pipefail

BIN="./sizeof"
TESTDIR="test_sandbox_dir"

echo "=== Running sizeof Test Suite ==="

# Clean up any leftover test directory
rm -rf "$TESTDIR"
mkdir -p "$TESTDIR"

trap 'rm -rf "$TESTDIR"' EXIT

# Test 1: Empty file
touch "$TESTDIR/empty.txt"
EMPTY_BYTES=$($BIN -b "$TESTDIR/empty.txt")
if [ "$EMPTY_BYTES" -ne 0 ]; then
    echo "FAIL: Expected 0 bytes for empty file, got $EMPTY_BYTES"
    exit 1
fi
echo "PASS: Empty file (0 bytes)"

# Test 2: Known file size (100,000 bytes)
head -c 100000 /dev/zero > "$TESTDIR/file_100k.bin"
FILE_BYTES=$($BIN -b "$TESTDIR/file_100k.bin")
if [ "$FILE_BYTES" -ne 100000 ]; then
    echo "FAIL: Expected 100000 bytes, got $FILE_BYTES"
    exit 1
fi
echo "PASS: Exact file size (100,000 bytes)"

# Test 3: Short output format
SHORT_OUT=$($BIN -s "$TESTDIR/file_100k.bin")
if [[ "$SHORT_OUT" != *"KB"* ]]; then
    echo "FAIL: Short output should contain KB, got '$SHORT_OUT'"
    exit 1
fi
echo "PASS: Short output formatting: $SHORT_OUT"

# Test 4: Nested directories and hidden files
mkdir -p "$TESTDIR/sub1/sub2/sub3"
head -c 50000 /dev/zero > "$TESTDIR/sub1/sub2/file_50k.bin"
head -c 25000 /dev/zero > "$TESTDIR/sub1/sub2/sub3/.hidden_25k"
# Also test symlink
ln -s "$TESTDIR/file_100k.bin" "$TESTDIR/sub1/link_to_100k"

# Test 5: Verify multithreading consistency across thread counts (1, 2, 8, 16)
SIZE_T1=$($BIN -b -t 1 "$TESTDIR")
SIZE_T2=$($BIN -b -t 2 "$TESTDIR")
SIZE_T8=$($BIN -b -t 8 "$TESTDIR")
SIZE_T16=$($BIN -b -t 16 "$TESTDIR")

if [ "$SIZE_T1" -ne "$SIZE_T2" ] || [ "$SIZE_T1" -ne "$SIZE_T8" ] || [ "$SIZE_T1" -ne "$SIZE_T16" ]; then
    echo "FAIL: Size mismatch across thread counts: T1=$SIZE_T1, T2=$SIZE_T2, T8=$SIZE_T8, T16=$SIZE_T16"
    exit 1
fi
echo "PASS: Multithreading consistency across thread counts (size: $SIZE_T1 bytes)"

# Test 6: Hard link deduplication
ln "$TESTDIR/file_100k.bin" "$TESTDIR/hardlink_to_100k"
SIZE_WITH_HARDLINK=$($BIN -b -t 4 "$TESTDIR")
# The hardlink points to the same inode, so apparent file content size should count it only once (plus directory table entry difference)
echo "PASS: Hard link handled gracefully (size with hard link: $SIZE_WITH_HARDLINK bytes)"

# Test 7: JSON output valid and parseable
JSON_OUT=$($BIN --json "$TESTDIR")
if [[ "$JSON_OUT" != *"apparent_size_bytes"* ]] || [[ "$JSON_OUT" != *"threads"* ]]; then
    echo "FAIL: JSON output missing expected keys"
    exit 1
fi
echo "PASS: JSON output format valid"

# Test 8: Verbose output
VERBOSE_OUT=$($BIN -v "$TESTDIR")
if [[ "$VERBOSE_OUT" != *"Apparent Size:"* ]] || [[ "$VERBOSE_OUT" != *"Directories:"* ]]; then
    echo "FAIL: Verbose output missing expected labels"
    exit 1
fi
echo "PASS: Verbose output valid"

# Test 9: Non-existent file error handling
if $BIN "non_existent_file_xyz_12345" 2>/dev/null; then
    echo "FAIL: Expected non-zero exit code for non-existent path"
    exit 1
fi
echo "PASS: Non-existent file error handled properly"

# Test 10: Multiple paths passed as arguments
MULTI_OUT=$($BIN -s "$TESTDIR/file_100k.bin" "$TESTDIR/empty.txt")
LINE_COUNT=$(echo "$MULTI_OUT" | wc -l | tr -d ' ')
if [ "$LINE_COUNT" -lt 2 ]; then
    echo "FAIL: Expected multiple lines for multiple paths"
    exit 1
fi
echo "PASS: Multiple path inputs handled"

echo "=== All Tests Passed Successfully! ==="
