#include "preset_loader.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#define TEST_DIR "/tmp/preset_loader_test"
#define TEST_PRESETS "/tmp/preset_loader_test/presets.json"

static int total_tests = 0;
static int passed_tests = 0;

void assert_true(const char *name, int condition) {
    total_tests++;
    if (condition) {
        printf("✓ PASS: %s\n", name);
        passed_tests++;
    } else {
        printf("✗ FAIL: %s\n", name);
    }
}

void assert_equal(const char *name, const char *expected, const char *actual) {
    total_tests++;
    if (strcmp(expected, actual) == 0) {
        printf("✓ PASS: %s\n", name);
        passed_tests++;
    } else {
        printf("✗ FAIL: %s - Expected '%s', got '%s'\n", name, expected, actual);
    }
}

void assert_null(const char *name, void *ptr) {
    total_tests++;
    if (ptr == NULL) {
        printf("✓ PASS: %s\n", name);
        passed_tests++;
    } else {
        printf("✗ FAIL: %s - Expected NULL, got pointer\n", name);
    }
}

void assert_not_null(const char *name, void *ptr) {
    total_tests++;
    if (ptr != NULL) {
        printf("✓ PASS: %s\n", name);
        passed_tests++;
    } else {
        printf("✗ FAIL: %s - Expected non-NULL pointer\n", name);
    }
}

void setup_test_dir() {
    mkdir(TEST_DIR, 0755);
}

void cleanup_test_dir() {
    system("rm -rf " TEST_DIR);
}

void write_malformed_json(const char *filepath) {
    FILE *f = fopen(filepath, "w");
    fprintf(f, "{ invalid json [ broken");
    fclose(f);
}

void write_valid_json(const char *filepath) {
    FILE *f = fopen(filepath, "w");
    fprintf(f, "{\n");
    fprintf(f, "  \"version\": \"1.0\",\n");
    fprintf(f, "  \"linux\": {\n");
    fprintf(f, "    \"copy\": {\n");
    fprintf(f, "      \"default\": {\n");
    fprintf(f, "        \"ffmpeg_args\": \"-c:v copy \",\n");
    fprintf(f, "        \"container\": \"mkv\"\n");
    fprintf(f, "      }\n");
    fprintf(f, "    },\n");
    fprintf(f, "    \"prores\": {\n");
    fprintf(f, "      \"default\": {\n");
    fprintf(f, "        \"ffmpeg_args\": \"-c:v prores -profile:v 2 \",\n");
    fprintf(f, "        \"container\": \"mov\"\n");
    fprintf(f, "      },\n");
    fprintf(f, "      \"hq\": {\n");
    fprintf(f, "        \"ffmpeg_args\": \"-c:v prores -profile:v 3 \",\n");
    fprintf(f, "        \"container\": \"mov\"\n");
    fprintf(f, "      }\n");
    fprintf(f, "    }\n");
    fprintf(f, "  }\n");
    fprintf(f, "}\n");
    fclose(f);
}

int main() {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     PRESET LOADER EXTENDED TESTS (C Implementation)       ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");

    setup_test_dir();

    // Test 1: Malformed JSON → fallback to built-in
    printf("=== Malformed JSON Handling ===\n");
    write_malformed_json(TEST_PRESETS);
    PresetDb *db = preset_db_load(TEST_DIR);
    assert_not_null("Load with malformed JSON returns database", db);
    if (db) {
        const PresetInfo *info = preset_db_get(db, "linux", "copy", "default");
        assert_not_null("Fallback provides copy preset", info);
        if (info) {
            assert_equal("Fallback preset has correct container", "mkv", info->container);
        }
        preset_db_free(db);
    }

    // Test 2: Valid JSON with multiple presets
    printf("\n=== Valid JSON with Multiple Presets ===\n");
    write_valid_json(TEST_PRESETS);
    db = preset_db_load(TEST_DIR);
    assert_not_null("Load valid JSON succeeds", db);
    if (db) {
        const PresetInfo *info = preset_db_get(db, "linux", "prores", "default");
        assert_not_null("Get prores/default preset", info);
        if (info) {
            assert_true("prores/default has prores ffmpeg_args", 
                        strstr(info->ffmpeg_args, "prores") != NULL);
        }
        
        info = preset_db_get(db, "linux", "prores", "hq");
        assert_not_null("Get prores/hq preset", info);
        if (info) {
            assert_true("prores/hq uses profile 3", 
                        strstr(info->ffmpeg_args, "profile:v 3") != NULL);
        }
        preset_db_free(db);
    }

    // Test 3: Missing file → fallback to built-in
    printf("\n=== Missing File Fallback ===\n");
    system("rm -f " TEST_PRESETS);
    db = preset_db_load(TEST_DIR);
    assert_not_null("Load from empty dir returns database", db);
    if (db) {
        const PresetInfo *info = preset_db_get(db, "linux", "copy", "default");
        assert_not_null("Fallback provides copy preset", info);
        if (info) {
            assert_equal("Fallback container is mkv", "mkv", info->container);
        }
        preset_db_free(db);
    }

    // Test 4: Environment variable handling
    printf("\n=== Placeholder Substitution with Defaults ===\n");
    char output[256];
    
    int ret = preset_substitute_placeholders(
        output, sizeof(output),
        "-hwaccel vaapi -hwaccel_device {vaapi_device}",
        "/dev/dri/renderD200", -1, 0
    );
    assert_true("VAAPI device substitution succeeds", ret == 0);
    assert_true("VAAPI device substituted correctly", 
               strstr(output, "/dev/dri/renderD200") != NULL);
    
    ret = preset_substitute_placeholders(
        output, sizeof(output),
        "-hwaccel vaapi -hwaccel_device {vaapi_device}",
        NULL, -1, 0
    );
    assert_true("VAAPI device default substitution succeeds", ret == 0);
    assert_true("VAAPI device defaults when NULL", 
               strstr(output, "/dev/dri/renderD128") != NULL);

    // Test 5: Performance benchmark
    printf("\n=== Performance Benchmark (10,000 lookups) ===\n");
    write_valid_json(TEST_PRESETS);
    db = preset_db_load(TEST_DIR);
    
    if (db) {
        clock_t start = clock();
        
        for (int i = 0; i < 10000; i++) {
            const char *preset_name = (i % 2 == 0) ? "default" : "hq";
            preset_db_get(db, "linux", "prores", preset_name);
        }
        
        clock_t end = clock();
        double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
        double per_lookup = (elapsed / 10000.0) * 1000000;  // microseconds
        
        printf("  Total time: %.3f ms\n", elapsed * 1000);
        printf("  Per lookup: %.2f µs\n", per_lookup);
        printf("  Lookups/sec: %d\n", (int)(10000.0 / elapsed));
        
        assert_true("Benchmark completed (< 100ms target)", elapsed < 0.1);
        
        preset_db_free(db);
    }

    // Test 6: List operations with valid JSON
    printf("\n=== List Operations ===\n");
    db = preset_db_load(TEST_DIR);
    if (db) {
        const char **codecs = NULL;
        int count = preset_db_list_codecs(db, "linux", &codecs);
        assert_true("List codecs returns at least 2", count >= 2);
        
        if (count > 0) {
            int found_prores = 0;
            for (int i = 0; i < count; i++) {
                if (strcmp(codecs[i], "prores") == 0) found_prores = 1;
            }
            assert_true("prores codec found in list", found_prores);
            free(codecs);
        }
        
        const char **presets = NULL;
        count = preset_db_list_presets(db, "linux", "prores", &presets);
        assert_true("List presets returns at least 2", count >= 2);
        
        if (count > 0) {
            int found_hq = 0;
            for (int i = 0; i < count; i++) {
                if (strcmp(presets[i], "hq") == 0) found_hq = 1;
            }
            assert_true("hq preset found in list", found_hq);
            free(presets);
        }
        
        preset_db_free(db);
    }

    // Test 7: Error message clarity
    printf("\n=== Error Message Clarity ===\n");
    db = preset_db_load(TEST_DIR);
    if (db) {
        preset_db_get(db, "invalid_platform", "copy", "default");
        const char *error = preset_get_last_error();
        assert_true("Error message contains 'Preset not found'", 
                   strstr(error, "Preset not found") != NULL);
        
        preset_db_free(db);
    }

    // Summary
    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║                    TEST SUMMARY                           ║\n");
    printf("╠════════════════════════════════════════════════════════════╣\n");
    printf("║  Total Tests:     %d                                       ║\n", total_tests);
    printf("║  Passed:          %d ✓                                     ║\n", passed_tests);
    printf("║  Failed:          %d ✗                                     ║\n", total_tests - passed_tests);
    printf("╚════════════════════════════════════════════════════════════╝\n\n");

    cleanup_test_dir();

    if (passed_tests == total_tests) {
        printf("✓ All extended tests passed!\n");
        return 0;
    } else {
        printf("✗ Some tests failed!\n");
        return 1;
    }
}
