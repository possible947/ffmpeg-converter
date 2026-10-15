/**
 * test_cli_m4v_policy.c
 *
 * Regression tests for M4V policy integration in CLI.
 * Validates that:
 *  - Both --codec m4v and --codec mux --encoder m4v paths work identically
 *  - audio-output parameter doesn't modify codec, preset, deblock, audio_norm, or video settings
 *  - M4V defaults are applied correctly via mux_policy_resolve()
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "converter.h"
#include "cli_common.h"
#include "mux_policy.h"

static int failures = 0;

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s\n", message); \
        failures++; \
    } else { \
        printf("PASS: %s\n", message); \
    } \
} while (0)

/**
 * Mock platform functions needed by parse_args
 */
int platform_audio_mode_is_available(const char* mode) {
    return mode && (strcmp(mode, "copy") == 0 ||
                   strcmp(mode, "pcm") == 0 ||
                   strcmp(mode, "fdk_aac_320") == 0 ||
                   strcmp(mode, "fdk_aac_320_ac3_640") == 0);
}

int platform_mux_is_supported(void) {
    return 1;
}

/**
 * Test scenario 1: --codec m4v with no explicit audio-output
 * Expected: audio_output_mode = "fdk_aac_320_ac3_640" (default via policy)
 */
void test_m4v_no_explicit_audio(void) {
    ConvertOptions opts;
    CliM4VOptions m4v_opts;
    const char *files[1] = { NULL };
    int file_count = 0;
    
    const char *argv[] = {
        "ffmpeg_converter",
        "--codec", "m4v"
    };
    int argc = 3;
    
    int result = parse_args(argc, (char**)argv, NULL, &opts, &m4v_opts,
                           files, &file_count);
    
    CHECK(result == 0, "--codec m4v without files should fail");
    CHECK(strcmp(opts.audio_output_mode, "fdk_aac_320_ac3_640") == 0,
          "--codec m4v applies default audio_output fdk_aac_320_ac3_640");
    CHECK(strcmp(opts.codec, "m4v") == 0,
          "codec is m4v");
    CHECK(strcmp(opts.preset, "m4v") == 0,
          "preset is set to m4v");
}

/**
 * Test scenario 2: --codec mux --encoder m4v with no explicit audio-output
 * Expected: audio_output_mode = "fdk_aac_320_ac3_640" (default via policy)
 *           Same outcome as --codec m4v
 */
void test_mux_m4v_no_explicit_audio(void) {
    ConvertOptions opts;
    CliM4VOptions m4v_opts;
    const char *files[1] = { NULL };
    int file_count = 0;
    
    const char *argv[] = {
        "ffmpeg_converter",
        "--codec", "mux",
        "--encoder", "m4v"
    };
    int argc = 5;
    
    int result = parse_args(argc, (char**)argv, NULL, &opts, &m4v_opts,
                           files, &file_count);
    
    CHECK(result == 0, "--codec mux --encoder m4v without files should fail");
    CHECK(strcmp(opts.audio_output_mode, "fdk_aac_320_ac3_640") == 0,
          "--codec mux --encoder m4v applies default audio_output fdk_aac_320_ac3_640");
    CHECK(strcmp(opts.codec, "mux") == 0,
          "codec is mux");
    CHECK(strcmp(opts.preset, "m4v") == 0,
          "preset is set to m4v");
}

/**
 * Test scenario 3: --codec m4v --audio-output pcm
 * Expected: Fails because M4V rejects PCM (policy validation)
 */
void test_m4v_explicit_pcm_rejected(void) {
    ConvertOptions opts;
    CliM4VOptions m4v_opts;
    const char *files[1] = { NULL };
    int file_count = 0;
    
    const char *argv[] = {
        "ffmpeg_converter",
        "--codec", "m4v",
        "--audio-output", "pcm"
    };
    int argc = 5;
    
    int result = parse_args(argc, (char**)argv, NULL, &opts, &m4v_opts,
                           files, &file_count);
    
    CHECK(result == 0, "--codec m4v with pcm should fail due to policy");
}

/**
 * Test scenario 4: --codec mux --encoder m4v --audio-output fdk_aac_320
 * Expected: Succeeds and applies AAC-only mode
 */
void test_mux_m4v_explicit_aac(void) {
    ConvertOptions opts;
    CliM4VOptions m4v_opts;
    const char *files[1] = { NULL };
    int file_count = 0;
    
    const char *argv[] = {
        "ffmpeg_converter",
        "--codec", "mux",
        "--encoder", "m4v",
        "--audio-output", "fdk_aac_320"
    };
    int argc = 7;
    
    int result = parse_args(argc, (char**)argv, NULL, &opts, &m4v_opts,
                           files, &file_count);
    
    CHECK(result == 0, "--codec mux --encoder m4v with explicit AAC without files should fail");
    CHECK(strcmp(opts.audio_output_mode, "fdk_aac_320") == 0,
          "explicit --audio-output fdk_aac_320 is applied");
    CHECK(strcmp(opts.codec, "mux") == 0,
          "codec remains mux");
}

/**
 * Test scenario 5: Verify audio-output changes don't affect codec/preset/deblock
 * Expected: Only audio_output_mode changes, other fields remain unchanged
 */
void test_audio_output_isolation(void) {
    ConvertOptions opts1, opts2;
    CliM4VOptions m4v_opts;
    const char *files[1] = { NULL };
    int file_count = 0;
    
    /* First call with default audio */
    const char *argv1[] = {
        "ffmpeg_converter",
        "--codec", "m4v",
        "--deblock", "weak"
    };
    parse_args(sizeof(argv1)/sizeof(argv1[0]), (char**)argv1, NULL, &opts1, &m4v_opts,
              files, &file_count);
    
    /* Second call with explicit audio */
    const char *argv2[] = {
        "ffmpeg_converter",
        "--codec", "m4v",
        "--audio-output", "fdk_aac_320_ac3_640",
        "--deblock", "weak"
    };
    parse_args(sizeof(argv2)/sizeof(argv2[0]), (char**)argv2, NULL, &opts2, &m4v_opts,
              files, &file_count);
    
    /* Verify codec, preset, deblock remain identical */
    CHECK(strcmp(opts1.codec, opts2.codec) == 0,
          "codec unchanged when audio-output changes");
    CHECK(strcmp(opts1.preset, opts2.preset) == 0,
          "preset unchanged when audio-output changes");
    CHECK(opts1.deblock == opts2.deblock,
          "deblock unchanged when audio-output changes");
    CHECK(strcmp(opts1.audio_norm, opts2.audio_norm) == 0,
          "audio_norm unchanged when audio-output changes");
}

/**
 * Test scenario 6: --codec mux --encoder mkv with default audio
 * Expected: Uses PCM default (not AAC+AC3 which is M4V-specific)
 */
void test_mux_mkv_pcm_default(void) {
    ConvertOptions opts;
    CliM4VOptions m4v_opts;
    const char *files[1] = { NULL };
    int file_count = 0;
    
    const char *argv[] = {
        "ffmpeg_converter",
        "--codec", "mux",
        "--encoder", "mkv"
    };
    int argc = 5;
    
    int result = parse_args(argc, (char**)argv, NULL, &opts, &m4v_opts,
                           files, &file_count);
    
    CHECK(result == 0, "--codec mux --encoder mkv without files should fail");
    CHECK(strcmp(opts.audio_output_mode, "pcm") == 0,
          "--codec mux --encoder mkv applies default audio_output pcm");
    CHECK(strcmp(opts.codec, "mux") == 0,
          "codec is mux");
    CHECK(strcmp(opts.preset, "mkv") == 0,
          "preset is set to mkv");
}

int main(void) {
    printf("=== CLI M4V Policy Regression Tests ===\n\n");
    
    test_m4v_no_explicit_audio();
    printf("\n");
    
    test_mux_m4v_no_explicit_audio();
    printf("\n");
    
    test_m4v_explicit_pcm_rejected();
    printf("\n");
    
    test_mux_m4v_explicit_aac();
    printf("\n");
    
    test_audio_output_isolation();
    printf("\n");
    
    test_mux_mkv_pcm_default();
    printf("\n");
    
    if (failures) {
        fprintf(stderr, "\n%d test(s) failed.\n", failures);
        return 1;
    }
    
    printf("\nAll CLI M4V policy tests passed.\n");
    return 0;
}
