#import "apple_m4v_creator.h"
#import <math.h>
#include <string.h>

static BOOL isExecutableFile(NSString *path) {
    if (path.length == 0) {
        return NO;
    }
    return [[NSFileManager defaultManager] isExecutableFileAtPath:path];
}

static NSString *trimmed(NSString *value) {
    return [value stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
}

static BOOL runTaskCapture(NSString *launchPath,
                           NSArray<NSString *> *arguments,
                           NSString **output,
                           int *exitCode,
                           NSString **errorText) {
    NSTask *task = [[NSTask alloc] init];
    NSPipe *pipe = [NSPipe pipe];
    task.standardOutput = pipe;
    task.standardError = pipe;
    task.executableURL = [NSURL fileURLWithPath:launchPath];
    task.arguments = arguments;

    NSError *launchError = nil;
    if (![task launchAndReturnError:&launchError]) {
        if (errorText) {
            *errorText = [NSString stringWithFormat:@"Failed to launch %@: %@", launchPath, launchError.localizedDescription ?: @"unknown"]; 
        }
        return NO;
    }

    NSData *data = [[pipe fileHandleForReading] readDataToEndOfFile];
    [task waitUntilExit];
    NSString *captured = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    if (!captured) {
        captured = @"";
    }

    if (output) {
        *output = captured;
    }
    if (exitCode) {
        *exitCode = task.terminationStatus;
    }
    return YES;
}

static void detectAacEncoders(NSString *ffmpegBin,
                              BOOL *hasAacAt,
                              BOOL *hasLibfdk,
                              BOOL *hasNativeAac) {
    static BOOL initialized = NO;
    static BOOL cachedHasAacAt = NO;
    static BOOL cachedHasLibfdk = NO;
    static BOOL cachedHasNativeAac = NO;

    if (!initialized) {
        NSString *output = @"";
        int exitCode = 0;
        NSString *errorText = nil;
        NSArray<NSString *> *args = @[@"-hide_banner", @"-v", @"error", @"-encoders"];
        if (runTaskCapture(ffmpegBin, args, &output, &exitCode, &errorText) && exitCode == 0) {
            cachedHasAacAt = [output rangeOfString:@" aac_at" options:NSCaseInsensitiveSearch].location != NSNotFound;
            cachedHasLibfdk = [output rangeOfString:@" libfdk_aac" options:NSCaseInsensitiveSearch].location != NSNotFound;
            cachedHasNativeAac = [output rangeOfString:@" aac " options:NSCaseInsensitiveSearch].location != NSNotFound;
        }
        initialized = YES;
    }

    if (hasAacAt) {
        *hasAacAt = cachedHasAacAt;
    }
    if (hasLibfdk) {
        *hasLibfdk = cachedHasLibfdk;
    }
    if (hasNativeAac) {
        *hasNativeAac = cachedHasNativeAac;
    }
}

static double parseRateToFps(NSString *rate) {
    NSString *value = trimmed(rate ?: @"");
    if (value.length == 0 || [value isEqualToString:@"0/0"]) {
        return 25.0;
    }

    NSArray<NSString *> *parts = [value componentsSeparatedByString:@"/"];
    if (parts.count == 2) {
        double n = parts[0].doubleValue;
        double d = parts[1].doubleValue;
        if (d != 0.0) {
            return n / d;
        }
        return 25.0;
    }

    double direct = value.doubleValue;
    if (direct > 0.0) {
        return direct;
    }
    return 25.0;
}

static BOOL isAllowedAppleM4VVideoCodec(NSString *codecName) {
    NSString *codec = [trimmed(codecName ?: @"") lowercaseString];
    return [codec isEqualToString:@"h264"] ||
           [codec isEqualToString:@"hevc"] ||
           [codec isEqualToString:@"prores"];
}

static NSString *makeChapterTimestamp(double seconds) {
    if (seconds < 0.0) {
        seconds = 0.0;
    }

    NSInteger h = (NSInteger)(seconds / 3600.0);
    seconds -= (double)h * 3600.0;
    NSInteger m = (NSInteger)(seconds / 60.0);
    seconds -= (double)m * 60.0;
    NSInteger s = (NSInteger)seconds;
    NSInteger ms = (NSInteger)llround((seconds - (double)s) * 1000.0);

    if (ms == 1000) {
        s += 1;
        ms = 0;
    }

    return [NSString stringWithFormat:@"%ld:%02ld:%02ld.%03ld", (long)h, (long)m, (long)s, (long)ms];
}

AppleM4VOptions AppleM4VDefaultOptions(void) {
    AppleM4VOptions opts;
    opts.videoTrackIndex = 0;
    opts.audioTrackIndex = 0;
    opts.aacQuality = 2;
    opts.ac3BitrateKbps = 640;
    opts.addChapters = YES;
    memset(opts.audioLang, 0, sizeof(opts.audioLang));
    strncpy(opts.audioLang, "rus", sizeof(opts.audioLang) - 1);
    return opts;
}

@interface AppleM4VCreator ()
@property (nonatomic, copy) NSString *ffmpegBin;
@property (nonatomic, copy) NSString *ffprobeBin;
@property (nonatomic, copy) NSString *mp4BoxBin;
@end

@implementation AppleM4VCreator

+ (nullable NSString *)resolveBinaryWithPrimaryName:(NSString *)name
                                            envVars:(NSArray<NSString *> *)envVars
                                         candidates:(NSArray<NSString *> *)candidates {
    NSDictionary<NSString *, NSString *> *env = NSProcessInfo.processInfo.environment;

    for (NSString *key in envVars) {
        NSString *value = trimmed(env[key] ?: @"");
        if (isExecutableFile(value)) {
            return value;
        }
    }

    for (NSString *path in candidates) {
        if (isExecutableFile(path)) {
            return path;
        }
    }

    NSString *pathEnv = env[@"PATH"] ?: @"";
    NSArray<NSString *> *dirs = [pathEnv componentsSeparatedByString:@":"];
    for (NSString *dir in dirs) {
        if (dir.length == 0) {
            continue;
        }
        NSString *candidate = [dir stringByAppendingPathComponent:name];
        if (isExecutableFile(candidate)) {
            return candidate;
        }
    }

    return nil;
}

- (instancetype)initWithFfmpegBin:(NSString *)ffmpegBin
                        ffprobeBin:(NSString *)ffprobeBin
                         mp4BoxBin:(NSString *)mp4BoxBin {
    self = [super init];
    if (self) {
        _ffmpegBin = [ffmpegBin copy];
        _ffprobeBin = [ffprobeBin copy];
        _mp4BoxBin = [mp4BoxBin copy];
    }
    return self;
}

- (double)probeFpsForInput:(NSString *)inputFile {
    NSString *output = @"";
    int code = 0;
    NSString *errorText = nil;

    NSArray<NSString *> *avgArgs = @[
        @"-v", @"error",
        @"-select_streams", @"v:0",
        @"-show_entries", @"stream=avg_frame_rate",
        @"-of", @"default=noprint_wrappers=1:nokey=1",
        inputFile
    ];
    if (runTaskCapture(self.ffprobeBin, avgArgs, &output, &code, &errorText) && code == 0) {
        double fps = parseRateToFps(output);
        if (fps > 0.0) {
            return fps;
        }
    }

    NSArray<NSString *> *rawArgs = @[
        @"-v", @"error",
        @"-select_streams", @"v:0",
        @"-show_entries", @"stream=r_frame_rate",
        @"-of", @"default=noprint_wrappers=1:nokey=1",
        inputFile
    ];
    if (runTaskCapture(self.ffprobeBin, rawArgs, &output, &code, &errorText) && code == 0) {
        return parseRateToFps(output);
    }

    return 25.0;
}

- (nullable NSString *)probeVideoCodecForInput:(NSString *)inputFile
                                    trackIndex:(NSInteger)trackIndex
                                     errorText:(NSString * _Nullable * _Nullable)errorText {
    NSString *output = @"";
    int code = 0;
    NSString *launchError = nil;
    NSArray<NSString *> *args = @[
        @"-v", @"error",
        @"-select_streams", [NSString stringWithFormat:@"v:%ld", (long)trackIndex],
        @"-show_entries", @"stream=codec_name",
        @"-of", @"default=noprint_wrappers=1:nokey=1",
        inputFile
    ];

    if (!runTaskCapture(self.ffprobeBin, args, &output, &code, &launchError)) {
        if (errorText) {
            *errorText = launchError ?: @"ffprobe failed while probing video codec";
        }
        return nil;
    }

    if (code != 0) {
        if (errorText) {
            NSString *tail = trimmed(output ?: @"");
            *errorText = [NSString stringWithFormat:@"ffprobe video codec probe failed (exit %d)%@%@",
                         code,
                         tail.length > 0 ? @"\n" : @"",
                         tail.length > 0 ? tail : @""];
        }
        return nil;
    }

    NSString *codec = trimmed(output ?: @"");
    if (codec.length == 0) {
        if (errorText) {
            *errorText = @"Unable to detect input video codec for Apple M4V.";
        }
        return nil;
    }

    return codec;
}

- (BOOL)runStepWithExecutable:(NSString *)executable
                    arguments:(NSArray<NSString *> *)arguments
                       stopFlag:(volatile BOOL *)stopFlag
                       stepName:(NSString *)stepName
                             log:(AppleM4VLogHandler)logHandler
                           stage:(AppleM4VStageHandler)stageHandler
                           error:(NSString * _Nullable * _Nullable)errorText {
    if (stopFlag && *stopFlag) {
        if (errorText) {
            *errorText = @"Stopped";
        }
        return NO;
    }

    if (stageHandler) {
        stageHandler(stepName);
    }

    NSString *output = @"";
    int code = 0;
    NSString *launchError = nil;

    if (!runTaskCapture(executable, arguments, &output, &code, &launchError)) {
        if (errorText) {
            *errorText = launchError ?: [NSString stringWithFormat:@"%@ failed to start", stepName];
        }
        return NO;
    }

    if (code != 0) {
        if (errorText) {
            NSString *tail = trimmed(output ?: @"");
            if (tail.length > 1200) {
                tail = [tail substringFromIndex:(tail.length - 1200)];
            }
            *errorText = [NSString stringWithFormat:@"%@ failed (exit %d)%@%@",
                         stepName,
                         code,
                         tail.length > 0 ? @"\n" : @"",
                         tail.length > 0 ? tail : @""];
        }
        return NO;
    }

    NSString *trimmedOutput = trimmed(output ?: @"");
    if (logHandler && trimmedOutput.length > 0) {
        logHandler([NSString stringWithFormat:@"%@ output:\n%@", stepName, trimmedOutput]);
    }

    return YES;
}

- (BOOL)buildChapterTextFromJsonData:(NSData *)jsonData
                          outputPath:(NSString *)chaptersPath {
    if (!jsonData || jsonData.length == 0) {
        return NO;
    }

    NSError *jsonError = nil;
    id root = [NSJSONSerialization JSONObjectWithData:jsonData options:0 error:&jsonError];
    if (jsonError || ![root isKindOfClass:[NSDictionary class]]) {
        return NO;
    }

    NSArray *chapters = ((NSDictionary *)root)[@"chapters"];
    if (![chapters isKindOfClass:[NSArray class]] || chapters.count == 0) {
        return NO;
    }

    NSMutableArray<NSString *> *lines = [[NSMutableArray alloc] init];
    NSInteger idx = 1;
    for (id item in chapters) {
        if (![item isKindOfClass:[NSDictionary class]]) {
            idx += 1;
            continue;
        }

        NSDictionary *chapter = (NSDictionary *)item;
        double startTime = [chapter[@"start_time"] doubleValue];

        NSString *title = [NSString stringWithFormat:@"Chapter %ld", (long)idx];
        NSDictionary *tags = chapter[@"tags"];
        if ([tags isKindOfClass:[NSDictionary class]]) {
            NSString *value = tags[@"title"];
            if ([value isKindOfClass:[NSString class]] && value.length > 0) {
                title = value;
            }
        }

        [lines addObject:[NSString stringWithFormat:@"%@ %@", makeChapterTimestamp(startTime), title]];
        idx += 1;
    }

    if (lines.count == 0) {
        return NO;
    }

    NSString *text = [[lines componentsJoinedByString:@"\n"] stringByAppendingString:@"\n"];
    NSError *writeError = nil;
    BOOL ok = [text writeToFile:chaptersPath atomically:YES encoding:NSUTF8StringEncoding error:&writeError];
    return ok && !writeError;
}

- (BOOL)createFromInput:(NSString *)inputFile
                 output:(NSString *)outputFile
                options:(AppleM4VOptions)options
               stopFlag:(volatile BOOL *)stopFlag
                    log:(AppleM4VLogHandler)logHandler
                  stage:(AppleM4VStageHandler)stageHandler
                  error:(NSString * _Nullable * _Nullable)errorText {
    if (!isExecutableFile(self.ffmpegBin) || !isExecutableFile(self.ffprobeBin) || !isExecutableFile(self.mp4BoxBin)) {
        if (errorText) {
            *errorText = @"Missing required tools (ffmpeg/ffprobe/MP4Box).";
        }
        return NO;
    }

    NSString *codecProbeError = nil;
    NSString *videoCodec = [self probeVideoCodecForInput:inputFile
                                              trackIndex:options.videoTrackIndex
                                               errorText:&codecProbeError];
    if (!videoCodec) {
        if (errorText) {
            *errorText = codecProbeError ?: @"Video codec probe failed for Apple M4V.";
        }
        return NO;
    }
    if (!isAllowedAppleM4VVideoCodec(videoCodec)) {
        if (errorText) {
            *errorText = [NSString stringWithFormat:@"Apple M4V supports only h264/hevc/prores input video streams. Detected: %@", videoCodec];
        }
        return NO;
    }
    if (logHandler) {
        logHandler([NSString stringWithFormat:@"Apple M4V video codec preflight: %@", videoCodec]);
    }

    NSString *tmpRoot = NSTemporaryDirectory();
    NSString *workDir = [tmpRoot stringByAppendingPathComponent:[NSString stringWithFormat:@"m4v_mux_%@", NSUUID.UUID.UUIDString]];
    NSError *mkError = nil;
    if (![[NSFileManager defaultManager] createDirectoryAtPath:workDir withIntermediateDirectories:YES attributes:nil error:&mkError]) {
        if (errorText) {
            *errorText = [NSString stringWithFormat:@"Failed to create temp dir: %@", mkError.localizedDescription ?: @"unknown"];
        }
        return NO;
    }

    BOOL ok = NO;
    @try {
        NSString *videoMp4 = [workDir stringByAppendingPathComponent:@"video_only.mp4"];
        NSString *aacM4a = [workDir stringByAppendingPathComponent:@"audio_aac.m4a"];
        NSString *ac3Mp4 = [workDir stringByAppendingPathComponent:@"audio_ac3.mp4"];
        NSString *chaptersJson = [workDir stringByAppendingPathComponent:@"chapters.json"];
        NSString *chaptersTxt = [workDir stringByAppendingPathComponent:@"chapters.txt"];

        double fps = [self probeFpsForInput:inputFile];
        NSString *fpsStr = [NSString stringWithFormat:@"%.6f", fps];

        NSArray<NSString *> *videoArgs = @[
            @"-y", @"-nostdin",
            @"-i", inputFile,
            @"-map", [NSString stringWithFormat:@"0:v:%ld", (long)options.videoTrackIndex],
            @"-c:v", @"copy",
            @"-an", @"-sn", @"-dn",
            @"-f", @"mp4",
            videoMp4
        ];
        if (![self runStepWithExecutable:self.ffmpegBin arguments:videoArgs stopFlag:stopFlag stepName:@"Apple M4V step 1/5: video copy" log:logHandler stage:stageHandler error:errorText]) {
            return NO;
        }

        BOOL hasAacAt = NO;
        BOOL hasLibfdk = NO;
        BOOL hasNativeAac = NO;
        detectAacEncoders(self.ffmpegBin, &hasAacAt, &hasLibfdk, &hasNativeAac);

        NSString *aacEncoder = @"aac";
        NSArray<NSString *> *aacCodecArgs = nil;
        if (hasAacAt) {
            aacEncoder = @"aac_at";
            aacCodecArgs = @[@"-c:a", @"aac_at", @"-q:a", @"2", @"-ar", @"48000"];
        } else if (hasLibfdk) {
            aacEncoder = @"libfdk_aac";
            aacCodecArgs = @[@"-c:a", @"libfdk_aac", @"-vbr", @"5", @"-ar", @"48000"];
        } else if (hasNativeAac) {
            aacEncoder = @"aac";
            aacCodecArgs = @[@"-c:a", @"aac", @"-profile:a", @"aac_low", @"-q:a", [NSString stringWithFormat:@"%ld", (long)options.aacQuality], @"-ar", @"48000"];
        } else {
            aacEncoder = @"aac";
            aacCodecArgs = @[@"-c:a", @"aac", @"-q:a", @"2", @"-ar", @"48000"];
        }

        if (logHandler) {
            logHandler([NSString stringWithFormat:@"Apple M4V AAC encoder selected: %@", aacEncoder]);
        }

        NSMutableArray<NSString *> *aacArgs = [NSMutableArray arrayWithArray:@[
            @"-y", @"-nostdin",
            @"-i", inputFile,
            @"-map", [NSString stringWithFormat:@"0:a:%ld", (long)options.audioTrackIndex]
        ]];
        [aacArgs addObjectsFromArray:aacCodecArgs];
        [aacArgs addObjectsFromArray:@[@"-f", @"mp4", aacM4a]];
        if (![self runStepWithExecutable:self.ffmpegBin arguments:aacArgs stopFlag:stopFlag stepName:@"Apple M4V step 2/5: AAC encode" log:logHandler stage:stageHandler error:errorText]) {
            return NO;
        }

        NSArray<NSString *> *ac3Args = @[
            @"-y", @"-nostdin",
            @"-i", inputFile,
            @"-map", [NSString stringWithFormat:@"0:a:%ld", (long)options.audioTrackIndex],
            @"-c:a", @"ac3",
            @"-b:a", [NSString stringWithFormat:@"%ldk", (long)options.ac3BitrateKbps],
            @"-f", @"mp4",
            ac3Mp4
        ];
        if (![self runStepWithExecutable:self.ffmpegBin arguments:ac3Args stopFlag:stopFlag stepName:@"Apple M4V step 3/5: AC3 encode" log:logHandler stage:stageHandler error:errorText]) {
            return NO;
        }

        NSString *videoAdd = [NSString stringWithFormat:@"%@#video:fps=%@:name=Video", videoMp4, fpsStr];
        NSString *lang = @"rus";
        if (options.audioLang[0] != '\0') {
            NSString *decoded = [NSString stringWithUTF8String:options.audioLang];
            if (decoded.length > 0) {
                lang = decoded;
            }
        }

        NSString *aacAdd = [NSString stringWithFormat:@"%@#audio:name=AAC:lang=%@", aacM4a, lang];
        NSString *ac3Add = [NSString stringWithFormat:@"%@#audio:name=AC3 %ldk:lang=%@", ac3Mp4, (long)options.ac3BitrateKbps, lang];
        NSArray<NSString *> *muxArgs = @[
            @"-new",
            @"-brand", @"M4V :0",
            @"-ab", @"mp42",
            @"-ab", @"isom",
            @"-add", videoAdd,
            @"-add", aacAdd,
            @"-add", ac3Add,
            outputFile
        ];
        if (![self runStepWithExecutable:self.mp4BoxBin arguments:muxArgs stopFlag:stopFlag stepName:@"Apple M4V step 4/5: MP4Box mux" log:logHandler stage:stageHandler error:errorText]) {
            return NO;
        }

        if (options.addChapters) {
            if (stageHandler) {
                stageHandler(@"Apple M4V step 5/5: chapters");
            }

            NSString *jsonOutput = @"";
            int chapterCode = 0;
            NSString *chapterError = nil;
            NSArray<NSString *> *chapterProbeArgs = @[
                @"-v", @"error",
                @"-print_format", @"json",
                @"-show_chapters",
                inputFile
            ];

            if (runTaskCapture(self.ffprobeBin, chapterProbeArgs, &jsonOutput, &chapterCode, &chapterError) && chapterCode == 0) {
                NSError *writeError = nil;
                [jsonOutput writeToFile:chaptersJson atomically:YES encoding:NSUTF8StringEncoding error:&writeError];
                if (!writeError) {
                    NSData *jsonData = [jsonOutput dataUsingEncoding:NSUTF8StringEncoding];
                    if ([self buildChapterTextFromJsonData:jsonData outputPath:chaptersTxt]) {
                        NSArray<NSString *> *chapArgs = @[@"-chap", chaptersTxt, outputFile];
                        NSString *chapterApplyError = nil;
                        if (![self runStepWithExecutable:self.mp4BoxBin arguments:chapArgs stopFlag:stopFlag stepName:@"Apple M4V chapters import" log:logHandler stage:nil error:&chapterApplyError]) {
                            if (logHandler && chapterApplyError.length > 0) {
                                logHandler([NSString stringWithFormat:@"Apple M4V chapters warning: %@", chapterApplyError]);
                            }
                        }
                    } else if (logHandler) {
                        logHandler(@"Apple M4V: no chapters found in source");
                    }
                }
            } else if (logHandler) {
                logHandler([NSString stringWithFormat:@"Apple M4V chapter probe warning: %@", chapterError ?: @"unknown"]);
            }
        }

        if (stopFlag && *stopFlag) {
            if (errorText) {
                *errorText = @"Stopped";
            }
            return NO;
        }

        ok = YES;
        return YES;
    }
    @finally {
        [[NSFileManager defaultManager] removeItemAtPath:workDir error:nil];
        if (!ok && stopFlag && *stopFlag) {
            [[NSFileManager defaultManager] removeItemAtPath:outputFile error:nil];
        }
    }
}

@end
