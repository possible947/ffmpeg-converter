/* Exercise the production controls without starting the application or probes. */
#define main macos_application_main
#include "main.m"
#undef main

#include <stdlib.h>

#define CHECK(...) do { \
    if (!(__VA_ARGS__)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__); \
        exit(1); \
    } \
} while (0)

static NSPopUpButton *popup(NSArray<NSString *> *titles) {
    NSPopUpButton *control = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [control addItemsWithTitles:titles];
    return control;
}

static void checkAudioRefresh(AppDelegate *delegate, NSString *preset) {
    NSArray<NSString *> *modes = delegate.audioOutputPopup.itemTitles;
    NSString *codec = delegate.codecPopup.titleOfSelectedItem;
    NSString *encoder = delegate.encoderPopup.titleOfSelectedItem;
    for (NSString *mode in modes) {
        [delegate.audioOutputPopup selectItemWithTitle:mode];
        [delegate onAudioOutputChanged:nil];
        CHECK([delegate.audioOutputPopup.titleOfSelectedItem isEqualToString:mode]);
        CHECK([delegate.profilePopup.titleOfSelectedItem isEqualToString:preset]);
        CHECK([delegate.codecPopup.titleOfSelectedItem isEqualToString:codec]);
        CHECK([delegate.encoderPopup.titleOfSelectedItem isEqualToString:encoder]);
        CHECK([delegate.profilePopup.itemTitles isEqualToArray:@[@"default", preset]]);
        [delegate onAudioNormChanged:nil];
        CHECK([delegate.audioOutputPopup.titleOfSelectedItem isEqualToString:mode]);
        CHECK([delegate.profilePopup.titleOfSelectedItem isEqualToString:preset]);
    }
}

int main(void) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        AppDelegate *delegate = [[AppDelegate alloc] init];
        delegate.codecPopup = popup(@[@"software", @"mux", @"hq_converter"]);
        delegate.encoderPopup = popup(@[@"prores_ks", @"m4v", @"prores", @"h264"]);
        delegate.profilePopup = popup(@[@"default", @"hq"]);
        [delegate.profilePopup selectItemWithTitle:@"hq"];
        delegate.audioOutputPopup = popup(@[@"copy"]);
        delegate.audioPopup = popup(@[@"none", @"loudness_norm_2pass"]);
        delegate.filePaths = [NSMutableArray array];
        delegate.videoTrackPath = @"";

        [delegate updateDependentControls];
        CHECK([delegate.audioOutputPopup.itemTitles isEqualToArray:
               @[@"pcm", @"fdk_aac_320", @"fdk_aac_320_ac3_640"]]);
        CHECK([delegate.audioOutputPopup.titleOfSelectedItem isEqualToString:@"pcm"]);
        checkAudioRefresh(delegate, @"hq");

        [delegate.audioOutputPopup selectItemWithTitle:@"pcm"];
        [delegate.codecPopup selectItemWithTitle:@"mux"];
        [delegate.encoderPopup selectItemWithTitle:@"m4v"];
        [delegate updateDependentControls];
        CHECK(delegate.audioOutputPopup.numberOfItems == 2);
        CHECK([delegate.audioOutputPopup.titleOfSelectedItem
               isEqualToString:@"fdk_aac_320_ac3_640"]);
        CHECK([delegate.audioOutputPopup itemWithTitle:@"fdk_aac_320"] != nil);
        checkAudioRefresh(delegate, @"hq");

        [delegate.codecPopup selectItemWithTitle:@"hq_converter"];
        [delegate.encoderPopup selectItemWithTitle:@"prores"];
        [delegate.profilePopup removeAllItems];
        [delegate.profilePopup addItemsWithTitles:@[@"default", @"hq-test-preset"]];
        [delegate.profilePopup selectItemWithTitle:@"hq-test-preset"];
        [delegate updateDependentControls];
        CHECK([delegate.audioOutputPopup.itemTitles isEqualToArray:@[@"pcm"]]);
        checkAudioRefresh(delegate, @"hq-test-preset");

        [delegate.encoderPopup selectItemWithTitle:@"h264"];
        [delegate updateDependentControls];
        MuxBuildProfile profile;
        CHECK(mux_build_profile_default(&profile));
        MuxAudioMode expected[4];
        size_t count = mux_policy_list_audio_modes(
            &profile, MUX_MODE_MKV, NULL, 0, MUX_VIDEO_H264, expected, 4);
        CHECK(delegate.audioOutputPopup.numberOfItems == (NSInteger)count);
        for (size_t i = 0; i < count; i++)
            CHECK([[delegate.audioOutputPopup itemTitleAtIndex:(NSInteger)i]
                   isEqualToString:[NSString stringWithUTF8String:mux_audio_mode_name(expected[i])]]);
        checkAudioRefresh(delegate, @"hq-test-preset");

        /* AAC remains selected across a compatible workflow change. */
        [delegate.audioOutputPopup selectItemWithTitle:@"fdk_aac_320"];
        [delegate.codecPopup selectItemWithTitle:@"software"];
        [delegate updateDependentControls];
        CHECK([delegate.audioOutputPopup.titleOfSelectedItem isEqualToString:@"fdk_aac_320"]);
        CHECK([delegate.audioOutputPopup itemWithTitle:@"copy"] == nil);
    }
    puts("macOS audio availability and preset isolation tests passed");
    return 0;
}
