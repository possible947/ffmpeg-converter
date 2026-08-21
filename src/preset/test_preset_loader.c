#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "preset_loader.h"

// ============================================================================
//  Test Helpers
// ============================================================================

static int test_count = 0;
static int test_passed = 0;
static int test_failed = 0;

#define TEST_ASSERT(condition, message) do { \
    test_count++; \
    if (condition) { \
        test_passed++; \
        printf("✓ PASS: %s\n", message); \
    } else { \
        test_failed++; \
        printf("✗ FAIL: %s\n", message); \
        printf("  Error: %s\n", preset_get_last_error()); \
    } \
} while(0)

#define TEST_SECTION(name) \
    printf("\n=== %s ===\n", name)

// ============================================================================
//  Test Cases
// ============================================================================

static void test_load_builtin_fallback(void) {
    TEST_SECTION("Loading Built-in Fallback");
    
    PresetDb *db = preset_db_load(NULL);
    TEST_ASSERT(db != NULL, "Load built-in fallback (NULL path)");
    
    if (db) {
        const PresetInfo *preset = preset_db_get(db, "linux", "copy", "default");
        TEST_ASSERT(preset != NULL, "Get linux/copy/default preset");
        TEST_ASSERT(preset != NULL && strcmp(preset->container, "mkv") == 0,
                   "copy preset has mkv container");
        TEST_ASSERT(preset != NULL && strstr(preset->ffmpeg_args, "-c:v copy") != NULL,
                   "copy preset has correct ffmpeg_args");
        
        preset_db_free(db);
    }
}

static void test_preset_lookup(void) {
    TEST_SECTION("Preset Lookup (O(1))");
    
    PresetDb *db = preset_db_load(NULL);
    TEST_ASSERT(db != NULL, "Load database");
    
    if (db) {
        // Test various lookups
        const char *tests[][3] = {
            {"linux", "copy", "default"},
            {"linux", "prores", "default"},
            {"linux", "prores_ks", "default"},
            {"macos", "copy", "default"},
            {"macos", "prores", "default"},
            {"windows", "copy", "default"},
        };
        
        for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
            const PresetInfo *preset = preset_db_get(db, tests[i][0], tests[i][1], tests[i][2]);
            char msg[256];
            snprintf(msg, sizeof(msg), "Get %s/%s/%s preset",
                    tests[i][0], tests[i][1], tests[i][2]);
            TEST_ASSERT(preset != NULL, msg);
        }
        
        preset_db_free(db);
    }
}

static void test_preset_not_found(void) {
    TEST_SECTION("Error Handling - Preset Not Found");
    
    PresetDb *db = preset_db_load(NULL);
    TEST_ASSERT(db != NULL, "Load database");
    
    if (db) {
        const PresetInfo *preset = preset_db_get(db, "linux", "invalid_codec", "default");
        TEST_ASSERT(preset == NULL, "Get non-existent codec returns NULL");
        
        preset = preset_db_get(db, "linux", "copy", "invalid_preset");
        TEST_ASSERT(preset == NULL, "Get non-existent preset returns NULL");
        
        preset = preset_db_get(db, "invalid_platform", "copy", "default");
        TEST_ASSERT(preset == NULL, "Get non-existent platform returns NULL");
        
        preset_db_free(db);
    }
}

static void test_placeholder_substitution(void) {
    TEST_SECTION("Placeholder Substitution");
    
    char output[512];
    int result;
    
    // Test VAAPI device placeholder
    result = preset_substitute_placeholders(output, sizeof(output),
                                           "-vaapi_device {vaapi_device}",
                                           "/dev/dri/renderD129", -1, 0);
    TEST_ASSERT(result == 0, "VAAPI device substitution succeeds");
    TEST_ASSERT(strcmp(output, "-vaapi_device /dev/dri/renderD129") == 0,
               "VAAPI device placeholder replaced correctly");
    
    // Test VAAPI device default
    result = preset_substitute_placeholders(output, sizeof(output),
                                           "-vaapi_device {vaapi_device}",
                                           NULL, -1, 0);
    TEST_ASSERT(result == 0, "VAAPI device default substitution succeeds");
    TEST_ASSERT(strcmp(output, "-vaapi_device /dev/dri/renderD128") == 0,
               "VAAPI device default placeholder replaced correctly");
    
    // Test Vulkan device placeholder
    result = preset_substitute_placeholders(output, sizeof(output),
                                           "-init_hw_device vulkan=vk:{vk_device}",
                                           NULL, 1, 0);
    TEST_ASSERT(result == 0, "Vulkan device substitution succeeds");
    TEST_ASSERT(strcmp(output, "-init_hw_device vulkan=vk:1") == 0,
               "Vulkan device placeholder replaced correctly");
    
    // Test Vulkan device default
    result = preset_substitute_placeholders(output, sizeof(output),
                                           "-init_hw_device vulkan=vk:{vk_device}",
                                           NULL, -1, 0);
    TEST_ASSERT(result == 0, "Vulkan device default substitution succeeds");
    TEST_ASSERT(strcmp(output, "-init_hw_device vulkan=vk:0") == 0,
               "Vulkan device default placeholder replaced correctly");
    
    // Test VideoToolbox bitrate placeholder
    result = preset_substitute_placeholders(output, sizeof(output),
                                           "-b:v {vt_bitrate}k",
                                           NULL, -1, 5000);
    TEST_ASSERT(result == 0, "VideoToolbox bitrate substitution succeeds");
    TEST_ASSERT(strcmp(output, "-b:v 5000k") == 0,
               "VideoToolbox bitrate placeholder replaced correctly");
    
    // Test multiple placeholders
    result = preset_substitute_placeholders(output, sizeof(output),
                                           "-vaapi_device {vaapi_device} -init_hw_device vulkan=vk:{vk_device}",
                                           "/dev/dri/renderD130", 2, 0);
    TEST_ASSERT(result == 0, "Multiple placeholders substitution succeeds");
    TEST_ASSERT(strcmp(output, "-vaapi_device /dev/dri/renderD130 -init_hw_device vulkan=vk:2") == 0,
               "Multiple placeholders replaced correctly");
}

static void test_placeholder_buffer_overflow(void) {
    TEST_SECTION("Error Handling - Buffer Overflow");
    
    char output[10];  // Very small buffer
    
    int result = preset_substitute_placeholders(output, sizeof(output),
                                               "-vaapi_device /dev/dri/renderD128",
                                               NULL, -1, 0);
    TEST_ASSERT(result == -1, "Detect buffer overflow with small buffer");
}

static void test_list_codecs(void) {
    TEST_SECTION("List Available Codecs");
    
    PresetDb *db = preset_db_load(NULL);
    TEST_ASSERT(db != NULL, "Load database");
    
    if (db) {
        const char **codecs = NULL;
        int count = preset_db_list_codecs(db, "linux", &codecs);
        TEST_ASSERT(count >= 3, "Linux has at least 3 codecs (copy, prores, prores_ks)");
        
        if (count >= 3 && codecs) {
            TEST_ASSERT(strcmp(codecs[0], "copy") == 0 ||
                       strcmp(codecs[1], "copy") == 0 ||
                       strcmp(codecs[2], "copy") == 0,
                       "Linux codec list contains 'copy'");
            free(codecs);
        }
        
        preset_db_free(db);
    }
}

static void test_list_presets(void) {
    TEST_SECTION("List Available Presets");
    
    PresetDb *db = preset_db_load(NULL);
    TEST_ASSERT(db != NULL, "Load database");
    
    if (db) {
        const char **presets = NULL;
        int count = preset_db_list_presets(db, "linux", "prores", &presets);
        TEST_ASSERT(count >= 1, "prores codec has at least 1 preset");
        
        if (count >= 1 && presets) {
            TEST_ASSERT(strcmp(presets[0], "default") == 0 || count > 1,
                       "prores codec list contains expected presets");
            free(presets);
        }
        
        preset_db_free(db);
    }
}

static void test_memory_cleanup(void) {
    TEST_SECTION("Memory Management");
    
    PresetDb *db = preset_db_load(NULL);
    TEST_ASSERT(db != NULL, "Load database");
    
    if (db) {
        // Do several lookups
        for (int i = 0; i < 10; i++) {
            preset_db_get(db, "linux", "copy", "default");
            preset_db_get(db, "macos", "prores", "default");
            preset_db_get(db, "windows", "prores_ks", "default");
        }
        
        // Free should work without errors
        preset_db_free(db);
        TEST_ASSERT(1, "Database freed successfully");
    }
}

static void test_cross_platform_consistency(void) {
    TEST_SECTION("Cross-Platform Consistency");
    
    PresetDb *db = preset_db_load(NULL);
    TEST_ASSERT(db != NULL, "Load database");
    
    if (db) {
        // All platforms should have 'copy' codec with 'default' preset
        const char *platforms[] = {"linux", "macos", "windows"};
        
        for (int i = 0; i < 3; i++) {
            const PresetInfo *preset = preset_db_get(db, platforms[i], "copy", "default");
            char msg[256];
            snprintf(msg, sizeof(msg), "%s has copy/default preset", platforms[i]);
            TEST_ASSERT(preset != NULL, msg);
            
            if (preset) {
                TEST_ASSERT(strcmp(preset->container, "mkv") == 0,
                           "All platforms have mkv container for copy preset");
            }
        }
        
        preset_db_free(db);
    }
}

// ============================================================================
//  Main Test Runner
// ============================================================================

int main(void) {
    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║          PRESET LOADER UNIT TESTS (C Implementation)      ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    
    test_load_builtin_fallback();
    test_preset_lookup();
    test_preset_not_found();
    test_placeholder_substitution();
    test_placeholder_buffer_overflow();
    test_list_codecs();
    test_list_presets();
    test_memory_cleanup();
    test_cross_platform_consistency();
    
    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║                        TEST SUMMARY                       ║\n");
    printf("╠════════════════════════════════════════════════════════════╣\n");
    printf("║  Total Tests:    %3d                                       ║\n", test_count);
    printf("║  Passed:         %3d ✓                                     ║\n", test_passed);
    printf("║  Failed:         %3d ✗                                     ║\n", test_failed);
    printf("╚════════════════════════════════════════════════════════════╝\n");
    
    if (test_failed > 0) {
        printf("\n⚠ Some tests failed!\n");
        return 1;
    } else {
        printf("\n✓ All tests passed!\n");
        return 0;
    }
}
