#import "converter_bridge.h"
#import "apple_m4v_creator.h"
#include "mux.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static __weak id s_activeBridge;

static int codec_is_mux(const char *codec) {
    return codec && strcmp(codec, "mux") == 0;
}

static int file_is_regular_readable(const char *path) {
    struct stat st;

    return path &&
           path[0] != '\0' &&
           stat(path, &st) == 0 &&
           S_ISREG(st.st_mode) &&
           access(path, R_OK) == 0;
}

static void resolve_effective_output_dir(const ConvertOptions *opts, char *out_dir, size_t out_dir_sz) {
    const char *home;

    if (!opts || !out_dir || out_dir_sz == 0)
        return;

    if (opts->output_dir[0] != '\0') {
        strncpy(out_dir, opts->output_dir, out_dir_sz - 1);
        out_dir[out_dir_sz - 1] = '\0';
        return;
    }

    home = getenv("HOME");
    if (!home || home[0] == '\0')
        home = ".";

    snprintf(out_dir, out_dir_sz, "%s/ffmpeg_converter", home);
}

static ConverterError run_bridge_mux_postprocess(const ConvertOptions *opts,
                                                 const ConverterCallbacks *cb,
                                                 const char *input_file) {
    ConvertOptions file_opts;
    MuxOptions mux_opts;
    char effective_output_dir[1024];

    if (!opts || !cb || !input_file)
        return ERR_INVALID_OPTIONS;

    memset(&file_opts, 0, sizeof(file_opts));
    file_opts = *opts;
    strcpy(file_opts.codec, "copy");
    resolve_effective_output_dir(opts, effective_output_dir, sizeof(effective_output_dir));
    strncpy(file_opts.output_dir, effective_output_dir, sizeof(file_opts.output_dir) - 1);
    file_opts.output_dir[sizeof(file_opts.output_dir) - 1] = '\0';

    memset(&mux_opts, 0, sizeof(mux_opts));
    converter_make_output_name(input_file, &file_opts, mux_opts.intermediate_file, sizeof(mux_opts.intermediate_file));
    strncpy(mux_opts.video_track_file, opts->video_track_path, sizeof(mux_opts.video_track_file) - 1);
    mux_opts.video_track_file[sizeof(mux_opts.video_track_file) - 1] = '\0';
    strncpy(mux_opts.output_file, mux_opts.intermediate_file, sizeof(mux_opts.output_file) - 1);
    mux_opts.output_file[sizeof(mux_opts.output_file) - 1] = '\0';
    mux_opts.overwrite = opts->overwrite;

    return mux_run_postprocess(&mux_opts, opts, cb);
}

@interface ConverterBridge () {
    Converter *_converter;
    BOOL _running;
    BOOL _appleRunning;
    volatile BOOL _appleStopFlag;
}

@property (nonatomic, copy) BridgeLogHandler logHandler;
@property (nonatomic, copy) BridgeStageHandler stageHandler;
@property (nonatomic, copy) BridgeProgressHandler progressHandler;
@property (nonatomic, copy) BridgeStatusHandler statusHandler;
@property (nonatomic, copy) BridgeCompletionHandler completionHandler;

- (void)emitLog:(NSString *)line;
- (void)emitStage:(NSString *)stage;
- (void)emitProgressPercent:(double)percent fps:(double)fps eta:(double)eta analysisMode:(BOOL)analysisMode;
- (void)emitStatus:(NSString *)status;
- (void)emitCompletion:(BOOL)success message:(NSString *)message;
- (void)configureBundledToolsEnvironment;
- (NSString *)resolveMp4BoxPath;

@end

static void cb_on_file_begin(const char *filename, int index, int total) {
    ConverterBridge *bridge = s_activeBridge;
    if (!bridge) return;
    NSString *line = [NSString stringWithFormat:@"[%d/%d] Processing: %s", index, total, filename ? filename : ""];
    [bridge emitLog:line];
    [bridge emitStatus:line];
}

static void cb_on_file_end(const char *filename, ConverterError status) {
    ConverterBridge *bridge = s_activeBridge;
    if (!bridge) return;
    NSString *line = [NSString stringWithFormat:@"%s: %s", filename ? filename : "", converter_error_string(status)];
    [bridge emitLog:line];
}

static void cb_on_stage(const char *stage) {
    ConverterBridge *bridge = s_activeBridge;
    if (!bridge) return;
    NSString *value = [NSString stringWithUTF8String:stage ? stage : ""];
    [bridge emitStage:value];
    [bridge emitStatus:[NSString stringWithFormat:@"Stage: %@", value]];
}

static void cb_on_progress_encode(float percent, float fps, float eta_seconds) {
    ConverterBridge *bridge = s_activeBridge;
    if (!bridge) return;
    [bridge emitProgressPercent:percent fps:fps eta:eta_seconds analysisMode:NO];
}

static void cb_on_progress_analysis(float percent, float eta_seconds) {
    ConverterBridge *bridge = s_activeBridge;
    if (!bridge) return;
    [bridge emitProgressPercent:percent fps:0.0f eta:eta_seconds analysisMode:YES];
}

static void cb_on_message(const char *text) {
    ConverterBridge *bridge = s_activeBridge;
    if (!bridge) return;
    [bridge emitLog:[NSString stringWithUTF8String:text ? text : ""]];
}

static void cb_on_error(const char *text, ConverterError code) {
    ConverterBridge *bridge = s_activeBridge;
    if (!bridge) return;
    NSString *line = [NSString stringWithFormat:@"ERROR: %s (%s)", text ? text : "", converter_error_string(code)];
    [bridge emitLog:line];
    [bridge emitStatus:line];
}

static void cb_on_complete(void) {
    ConverterBridge *bridge = s_activeBridge;
    if (!bridge) return;
    [bridge emitLog:@"All files processed."];
}

@implementation ConverterBridge

static NSString *appleOutputNameForSource(NSString *sourcePath, NSString *outputDir) {
    NSString *base = [[sourcePath lastPathComponent] stringByDeletingPathExtension];
    NSString *name = [base stringByAppendingString:@".m4v"];
    return [outputDir stringByAppendingPathComponent:name];
}

static NSString *converterOutputNameForInput(NSString *inputPath, const ConvertOptions *opts, NSString *outputDir) {
    ConvertOptions copy;
    char out_path[1024];

    memset(&copy, 0, sizeof(copy));
    copy = *opts;
    if (outputDir.length > 0) {
        strncpy(copy.output_dir, outputDir.UTF8String, sizeof(copy.output_dir) - 1);
        copy.output_dir[sizeof(copy.output_dir) - 1] = '\0';
    }

    converter_make_output_name(inputPath.UTF8String, &copy, out_path, sizeof(out_path));
    return [NSString stringWithUTF8String:out_path];
}

static BOOL wasFileProducedAfter(NSString *path, NSDate *threshold) {
    if (path.length == 0 || !threshold) {
        return NO;
    }

    NSDictionary<NSFileAttributeKey, id> *attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:path error:nil];
    NSDate *modDate = attrs[NSFileModificationDate];
    if (![modDate isKindOfClass:[NSDate class]]) {
        return NO;
    }

    return [modDate compare:threshold] != NSOrderedAscending;
}

static int locale_is_utf8(const char *value) {
    if (!value || value[0] == '\0') {
        return 0;
    }
    return strstr(value, "UTF-8") != NULL ||
           strstr(value, "utf-8") != NULL ||
           strstr(value, "UTF8") != NULL ||
           strstr(value, "utf8") != NULL;
}

- (void)configureBundledToolsEnvironment {
    NSString *resourcePath = [[NSBundle mainBundle] resourcePath];
    if (resourcePath.length == 0) {
        return;
    }

    NSString *binDir = [resourcePath stringByAppendingPathComponent:@"bin"];
    NSString *ffmpegPath = [binDir stringByAppendingPathComponent:@"ffmpeg"];
    NSString *ffprobePath = [binDir stringByAppendingPathComponent:@"ffprobe"];
    NSString *mp4boxPath = [binDir stringByAppendingPathComponent:@"MP4Box"];
    NSString *mkvmergePath = [binDir stringByAppendingPathComponent:@"mkvmerge"];

    NSFileManager *fm = [NSFileManager defaultManager];
    BOOL hasFfmpeg = [fm isExecutableFileAtPath:ffmpegPath];
    BOOL hasFfprobe = [fm isExecutableFileAtPath:ffprobePath];
    BOOL hasMp4box = [fm isExecutableFileAtPath:mp4boxPath];
    BOOL hasMkvmerge = [fm isExecutableFileAtPath:mkvmergePath];

    if (hasFfmpeg) {
        setenv("FFMPEG", ffmpegPath.UTF8String, 1);
        setenv("FFMPEG_BIN", ffmpegPath.UTF8String, 1);
    }
    if (hasFfprobe) {
        setenv("FFPROBE", ffprobePath.UTF8String, 1);
        setenv("FFPROBE_BIN", ffprobePath.UTF8String, 1);
    }
    if (hasMp4box) {
        setenv("MP4BOX_BIN", mp4boxPath.UTF8String, 1);
    }
    if (hasMkvmerge) {
        setenv("MKVMERGE_BIN", mkvmergePath.UTF8String, 1);
    }

    if (hasFfmpeg || hasFfprobe || hasMp4box || hasMkvmerge) {
        const char *pathEnv = getenv("PATH");
        NSString *currentPath = pathEnv ? [NSString stringWithUTF8String:pathEnv] : @"";
        if ([currentPath rangeOfString:binDir].location == NSNotFound) {
            NSString *updated = currentPath.length > 0 ? [NSString stringWithFormat:@"%@:%@", binDir, currentPath]
                                                      : binDir;
            setenv("PATH", updated.UTF8String, 1);
        }
    }

    /* Finder-launched apps often inherit C locale; force UTF-8 for tool paths. */
    {
        const char *lcAll = getenv("LC_ALL");
        const char *lcCType = getenv("LC_CTYPE");
        const char *lang = getenv("LANG");

        if (lcAll && !locale_is_utf8(lcAll)) {
            setenv("LC_ALL", "en_US.UTF-8", 1);
        }
        if (!locale_is_utf8(getenv("LC_ALL")) &&
            !locale_is_utf8(lcCType) &&
            !locale_is_utf8(lang)) {
            setenv("LANG", "en_US.UTF-8", 1);
            setenv("LC_CTYPE", "en_US.UTF-8", 1);
        }
    }
}

- (NSString *)resolveMp4BoxPath {
    NSString *resourcePath = [[NSBundle mainBundle] resourcePath];
    NSString *bundlePath = resourcePath.length > 0 ? [resourcePath stringByAppendingPathComponent:@"bin/MP4Box"] : @"";
    return [AppleM4VCreator resolveBinaryWithPrimaryName:@"MP4Box"
                                                 envVars:@[@"MP4BOX_BIN"]
                                              candidates:@[
                                                  bundlePath,
                                                  @"/opt/local/bin/MP4Box",
                                                  @"/opt/homebrew/bin/MP4Box",
                                                  @"/usr/local/bin/MP4Box"
                                              ]];
}

- (NSString *)defaultOutputDirectory {
    return [NSHomeDirectory() stringByAppendingPathComponent:@"ffmpeg_converter"];
}

- (BOOL)ensureDefaultOutputDirectoryExists:(NSError * _Nullable * _Nullable)error {
    NSString *path = [self defaultOutputDirectory];
    return [[NSFileManager defaultManager] createDirectoryAtPath:path
                                     withIntermediateDirectories:YES
                                                      attributes:nil
                                                           error:error];
}

- (ConvertOptions)makeOptionsWithCodec:(NSString *)codec
                                                             profile:(NSInteger)profile
                                                             deblock:(NSInteger)deblock
                             audioNorm:(NSString *)audioNorm
                         audioOutputMode:(NSString *)audioOutputMode
                          videoTrackPath:(NSString *)videoTrackPath
                                                                 genre:(NSInteger)genre
                             overwrite:(BOOL)overwrite
                             outputDir:(NSString *)outputDir {
    ConvertOptions opts;
    memset(&opts, 0, sizeof(opts));

    if (codec.length > 0) {
        strncpy(opts.codec, codec.UTF8String, sizeof(opts.codec) - 1);
    }

    if (audioNorm.length > 0) {
        strncpy(opts.audio_norm, audioNorm.UTF8String, sizeof(opts.audio_norm) - 1);
    }

    if (audioOutputMode.length > 0) {
        strncpy(opts.audio_output_mode, audioOutputMode.UTF8String, sizeof(opts.audio_output_mode) - 1);
    } else {
        strncpy(opts.audio_output_mode, "pcm", sizeof(opts.audio_output_mode) - 1);
    }

    if (videoTrackPath.length > 0) {
        strncpy(opts.video_track_path, videoTrackPath.UTF8String, sizeof(opts.video_track_path) - 1);
    }

    opts.profile = (int)profile;
    opts.deblock = (int)deblock;
    opts.genre = (int)genre;

    opts.overwrite = overwrite ? 1 : 0;

    NSString *resolvedOutput = outputDir.length > 0 ? outputDir : [self defaultOutputDirectory];
    strncpy(opts.output_dir, resolvedOutput.UTF8String, sizeof(opts.output_dir) - 1);

    return opts;
}

- (BOOL)isRunning {
    @synchronized (self) {
        return _running;
    }
}

- (BOOL)isAppleM4VRunning {
    @synchronized (self) {
        return _appleRunning;
    }
}

- (void)startConversionWithOptions:(ConvertOptions)options
                             files:(NSArray<NSString *> *)files
                                log:(BridgeLogHandler)logHandler
                              stage:(BridgeStageHandler)stageHandler
                           progress:(BridgeProgressHandler)progressHandler
                             status:(BridgeStatusHandler)statusHandler
                         completion:(BridgeCompletionHandler)completionHandler {
    if (files.count == 0) {
        if (completionHandler) {
            dispatch_async(dispatch_get_main_queue(), ^{
                completionHandler(NO, @"No input files selected");
            });
        }
        return;
    }

    @synchronized (self) {
        if (_running || _appleRunning) {
            if (completionHandler) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    completionHandler(NO, @"Another job is already running");
                });
            }
            return;
        }
        _running = YES;
        self.logHandler = logHandler;
        self.stageHandler = stageHandler;
        self.progressHandler = progressHandler;
        self.statusHandler = statusHandler;
        self.completionHandler = completionHandler;
    }

    [self configureBundledToolsEnvironment];

    NSArray<NSString *> *copiedFiles = [files copy];
    ConvertOptions optsCopy = options;

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        s_activeBridge = self;

        int fileCount = (int)copiedFiles.count;
        char **cFiles = calloc((size_t)fileCount, sizeof(char *));
        for (int i = 0; i < fileCount; i++) {
            cFiles[i] = strdup(copiedFiles[(NSUInteger)i].UTF8String);
        }

        Converter *c = converter_create();
        if (!c) {
            [self emitCompletion:NO message:@"Failed to create converter"];
            for (int i = 0; i < fileCount; i++) free(cFiles[i]);
            free(cFiles);
            @synchronized (self) { _running = NO; }
            s_activeBridge = nil;
            return;
        }

        @synchronized (self) { _converter = c; }

        ConverterCallbacks cb = {
            .on_file_begin = cb_on_file_begin,
            .on_file_end = cb_on_file_end,
            .on_stage = cb_on_stage,
            .on_progress_encode = cb_on_progress_encode,
            .on_progress_analysis = cb_on_progress_analysis,
            .on_message = cb_on_message,
            .on_error = cb_on_error,
            .on_complete = cb_on_complete
        };

        converter_set_callbacks(c, &cb);
        ConverterError err;
        ConvertOptions workOpts = optsCopy;

        if (codec_is_mux(optsCopy.codec)) {
            if (fileCount != 1) {
                err = ERR_INVALID_OPTIONS;
                [self emitLog:@"Mux mode requires exactly one source file."];
                [self emitStatus:@"Mux mode requires exactly one source file."];
                goto finish_conversion;
            }

            if (!file_is_regular_readable(optsCopy.video_track_path)) {
                err = ERR_INVALID_OPTIONS;
                [self emitLog:@"Mux mode requires a readable video track file."];
                [self emitStatus:@"Mux mode requires a readable video track file."];
                goto finish_conversion;
            }

            strcpy(workOpts.codec, "copy");
            workOpts.profile = 0;
            workOpts.deblock = 0;
        }

        err = converter_set_options(c, &workOpts);
        if (err != ERR_OK) {
            goto finish_conversion;
        }

        err = converter_process_files(c, (const char **)cFiles, fileCount);
        if (err == ERR_OK && codec_is_mux(optsCopy.codec)) {
            err = run_bridge_mux_postprocess(&optsCopy, &cb, cFiles[0]);
        }

finish_conversion:

        converter_destroy(c);
        @synchronized (self) { _converter = NULL; }

        for (int i = 0; i < fileCount; i++) free(cFiles[i]);
        free(cFiles);

        BOOL ok = (err == ERR_OK);
        NSString *message = nil;
        if (err == ERR_SKIP_FILE) {
            message = @"Stopped";
        } else if (ok) {
            message = @"Completed";
        } else {
            message = [NSString stringWithUTF8String:converter_error_string(err)];
        }
        [self emitCompletion:ok message:message];

        @synchronized (self) {
            _running = NO;
            self.logHandler = nil;
            self.stageHandler = nil;
            self.progressHandler = nil;
            self.statusHandler = nil;
            self.completionHandler = nil;
        }

        s_activeBridge = nil;
    });
}

- (void)startAppleM4VForFiles:(NSArray<NSString *> *)files
                     outputDir:(NSString *)outputDir
                     overwrite:(BOOL)overwrite
                                editBeforeMux:(BOOL)editBeforeMux
                                convertOptions:(ConvertOptions)convertOptions
                                    appleOptions:(AppleM4VOptions)appleOptions
                           log:(BridgeLogHandler)logHandler
                         stage:(BridgeStageHandler)stageHandler
                        status:(BridgeStatusHandler)statusHandler
                    completion:(BridgeCompletionHandler)completionHandler {
    if (files.count == 0) {
        if (completionHandler) {
            dispatch_async(dispatch_get_main_queue(), ^{
                completionHandler(NO, @"No input files selected");
            });
        }
        return;
    }

    @synchronized (self) {
        if (_running || _appleRunning) {
            if (completionHandler) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    completionHandler(NO, @"Another job is already running");
                });
            }
            return;
        }

        _appleRunning = YES;
        _appleStopFlag = NO;
        self.logHandler = logHandler;
        self.stageHandler = stageHandler;
        self.statusHandler = statusHandler;
        self.completionHandler = completionHandler;
    }

    [self configureBundledToolsEnvironment];

    NSString *ffmpegPath = [AppleM4VCreator resolveBinaryWithPrimaryName:@"ffmpeg"
                                                                 envVars:@[@"FFMPEG", @"FFMPEG_BIN"]
                                                              candidates:@[]];
    NSString *ffprobePath = [AppleM4VCreator resolveBinaryWithPrimaryName:@"ffprobe"
                                                                  envVars:@[@"FFPROBE", @"FFPROBE_BIN"]
                                                               candidates:@[]];
    NSString *mp4boxPath = [self resolveMp4BoxPath];

    if (ffmpegPath.length == 0 || ffprobePath.length == 0 || mp4boxPath.length == 0) {
        [self emitCompletion:NO message:@"Missing ffmpeg/ffprobe/MP4Box. Install GPAC or bundle MP4Box into app Resources/bin."];
        @synchronized (self) {
            _appleRunning = NO;
            self.logHandler = nil;
            self.stageHandler = nil;
            self.progressHandler = nil;
            self.statusHandler = nil;
            self.completionHandler = nil;
        }
        return;
    }

    NSString *resolvedOutput = outputDir.length > 0 ? outputDir : [self defaultOutputDirectory];
    NSError *mkdirError = nil;
    if (![[NSFileManager defaultManager] createDirectoryAtPath:resolvedOutput
                                   withIntermediateDirectories:YES
                                                    attributes:nil
                                                         error:&mkdirError]) {
        [self emitCompletion:NO message:[NSString stringWithFormat:@"Output dir error: %@", mkdirError.localizedDescription ?: @"unknown"]];
        @synchronized (self) {
            _appleRunning = NO;
            self.logHandler = nil;
            self.stageHandler = nil;
            self.progressHandler = nil;
            self.statusHandler = nil;
            self.completionHandler = nil;
        }
        return;
    }

    NSArray<NSString *> *copiedFiles = [files copy];
    AppleM4VCreator *creator = [[AppleM4VCreator alloc] initWithFfmpegBin:ffmpegPath
                                                                 ffprobeBin:ffprobePath
                                                                  mp4BoxBin:mp4boxPath];

    ConvertOptions convertOptsCopy = convertOptions;
    convertOptsCopy.overwrite = overwrite ? 1 : 0;
    strncpy(convertOptsCopy.output_dir, resolvedOutput.UTF8String, sizeof(convertOptsCopy.output_dir) - 1);
    convertOptsCopy.output_dir[sizeof(convertOptsCopy.output_dir) - 1] = '\0';

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSInteger successCount = 0;
        NSInteger failCount = 0;
        NSMutableArray<NSString *> *errors = [[NSMutableArray alloc] init];

        AppleM4VOptions opts = appleOptions;

        if (editBeforeMux) {
            [self emitLog:@"Apple m4v creator: edit-before-mux enabled (running main conversion first)..."];
            NSDate *conversionStart = [NSDate date];

            s_activeBridge = self;

            int fileCount = (int)copiedFiles.count;
            char **cFiles = calloc((size_t)fileCount, sizeof(char *));
            for (int i = 0; i < fileCount; i++) {
                cFiles[i] = strdup(copiedFiles[(NSUInteger)i].UTF8String);
            }

            Converter *c = converter_create();
            if (!c) {
                [self emitCompletion:NO message:@"Failed to create converter for edit-before-mux mode"];
                for (int i = 0; i < fileCount; i++) {
                    free(cFiles[i]);
                }
                free(cFiles);
                @synchronized (self) {
                    _appleRunning = NO;
                    _appleStopFlag = NO;
                    self.logHandler = nil;
                    self.stageHandler = nil;
                    self.progressHandler = nil;
                    self.statusHandler = nil;
                    self.completionHandler = nil;
                }
                s_activeBridge = nil;
                return;
            }

            ConverterCallbacks cb = {
                .on_file_begin = cb_on_file_begin,
                .on_file_end = cb_on_file_end,
                .on_stage = cb_on_stage,
                .on_progress_encode = cb_on_progress_encode,
                .on_progress_analysis = cb_on_progress_analysis,
                .on_message = cb_on_message,
                .on_error = cb_on_error,
                .on_complete = cb_on_complete
            };

            @synchronized (self) {
                _converter = c;
            }

            converter_set_callbacks(c, &cb);
            converter_set_options(c, &convertOptsCopy);
            ConverterError convErr = converter_process_files(c, (const char **)cFiles, fileCount);
            converter_destroy(c);

            @synchronized (self) {
                _converter = NULL;
            }

            for (int i = 0; i < fileCount; i++) {
                free(cFiles[i]);
            }
            free(cFiles);
            s_activeBridge = nil;

            if (convErr != ERR_OK) {
                if (convErr == ERR_SKIP_FILE || _appleStopFlag) {
                    [self emitCompletion:NO message:@"Stopped"];
                } else {
                    [self emitCompletion:NO message:[NSString stringWithUTF8String:converter_error_string(convErr)]];
                }

                @synchronized (self) {
                    _appleRunning = NO;
                    _appleStopFlag = NO;
                    self.logHandler = nil;
                    self.stageHandler = nil;
                    self.progressHandler = nil;
                    self.statusHandler = nil;
                    self.completionHandler = nil;
                }
                return;
            }

            for (NSString *sourceFile in copiedFiles) {
                NSString *convertedPath = converterOutputNameForInput(sourceFile, &convertOptsCopy, resolvedOutput);
                if (!wasFileProducedAfter(convertedPath, conversionStart)) {
                    NSString *line = [NSString stringWithFormat:@"Edit-before-mux source not freshly produced: %@", convertedPath];
                    [errors addObject:line];
                }
            }

            if (errors.count > 0) {
                NSString *details = [errors componentsJoinedByString:@" | "];
                [self emitCompletion:NO message:[NSString stringWithFormat:@"Edit-before-mux failed: %@", details]];
                @synchronized (self) {
                    _appleRunning = NO;
                    _appleStopFlag = NO;
                    self.logHandler = nil;
                    self.stageHandler = nil;
                    self.progressHandler = nil;
                    self.statusHandler = nil;
                    self.completionHandler = nil;
                }
                return;
            }
        }

        [self emitLog:[NSString stringWithFormat:@"Apple m4v creator started for %lu file(s)", (unsigned long)copiedFiles.count]];
        [self emitStatus:@"Apple m4v creator: processing..."];

        NSInteger index = 1;
        for (NSString *sourceFile in copiedFiles) {
            if (_appleStopFlag) {
                break;
            }

            NSString *m4vSource = sourceFile;
            if (editBeforeMux) {
                m4vSource = converterOutputNameForInput(sourceFile, &convertOptsCopy, resolvedOutput);
            }
            NSString *outputFile = appleOutputNameForSource(editBeforeMux ? sourceFile : m4vSource, resolvedOutput);
            [self emitLog:[NSString stringWithFormat:@"[%ld/%lu] Apple m4v: %@ -> %@",
                           (long)index,
                           (unsigned long)copiedFiles.count,
                           m4vSource,
                           outputFile]];

            BOOL isDir = NO;
            if (![[NSFileManager defaultManager] fileExistsAtPath:m4vSource isDirectory:&isDir] || isDir) {
                failCount += 1;
                [errors addObject:[NSString stringWithFormat:@"Input file not found: %@", m4vSource]];
                index += 1;
                continue;
            }

            if ([[NSFileManager defaultManager] fileExistsAtPath:outputFile]) {
                if (!overwrite) {
                    failCount += 1;
                    [errors addObject:[NSString stringWithFormat:@"Output exists (enable overwrite): %@", outputFile]];
                    index += 1;
                    continue;
                }
                NSError *rmError = nil;
                if (![[NSFileManager defaultManager] removeItemAtPath:outputFile error:&rmError]) {
                    failCount += 1;
                    [errors addObject:[NSString stringWithFormat:@"Cannot overwrite output: %@", rmError.localizedDescription ?: outputFile]];
                    index += 1;
                    continue;
                }
            }

            NSString *createError = nil;
            BOOL ok = [creator createFromInput:m4vSource
                                        output:outputFile
                                       options:opts
                                      stopFlag:&_appleStopFlag
                                           log:^(NSString *line) {
                                               [self emitLog:line];
                                           }
                                         stage:^(NSString *stageName) {
                                             [self emitStage:stageName];
                                             [self emitStatus:stageName];
                                         }
                                         error:&createError];
            if (ok) {
                successCount += 1;
                [self emitLog:[NSString stringWithFormat:@"Apple m4v creator OK: %@", outputFile]];
            } else if (_appleStopFlag && [createError isEqualToString:@"Stopped"]) {
                break;
            } else {
                failCount += 1;
                NSString *errLine = [NSString stringWithFormat:@"%@: %@", sourceFile.lastPathComponent, createError ?: @"unknown error"];
                [errors addObject:errLine];
                [self emitLog:[NSString stringWithFormat:@"Apple m4v creator ERROR: %@", errLine]];
            }

            if (ok && editBeforeMux) {
                NSError *cleanupError = nil;
                if (![[NSFileManager defaultManager] removeItemAtPath:m4vSource error:&cleanupError]) {
                    [self emitLog:[NSString stringWithFormat:@"Apple m4v warning: failed to delete temporary converted file: %@", m4vSource]];
                }
            }

            index += 1;
        }

        BOOL stopped = _appleStopFlag;
        NSString *message = nil;
        BOOL success = NO;
        if (stopped) {
            message = @"Stopped";
        } else if (failCount == 0 && successCount > 0) {
            success = YES;
            message = [NSString stringWithFormat:@"Apple m4v done (ok=%ld, failed=%ld)", (long)successCount, (long)failCount];
        } else if (failCount == 0 && successCount == 0) {
            message = @"Apple m4v finished: no files processed";
        } else {
            NSString *details = errors.count > 0 ? [errors componentsJoinedByString:@" | "] : @"No files processed";
            message = [NSString stringWithFormat:@"Apple m4v finished with errors (ok=%ld, failed=%ld): %@",
                       (long)successCount,
                       (long)failCount,
                       details];
        }

        [self emitCompletion:success message:message];

        @synchronized (self) {
            _appleRunning = NO;
            _appleStopFlag = NO;
            self.logHandler = nil;
            self.stageHandler = nil;
            self.progressHandler = nil;
            self.statusHandler = nil;
            self.completionHandler = nil;
        }
    });
}

- (void)stopConversion {
    @synchronized (self) {
        if (_converter) {
            converter_stop(_converter);
        }
    }
}

- (void)stopAppleM4V {
    @synchronized (self) {
        _appleStopFlag = YES;
        if (_converter) {
            converter_stop(_converter);
        }
    }
}

- (void)emitLog:(NSString *)line {
    BridgeLogHandler handler = self.logHandler;
    if (!handler) return;
    dispatch_async(dispatch_get_main_queue(), ^{ handler(line); });
}

- (void)emitStage:(NSString *)stage {
    BridgeStageHandler handler = self.stageHandler;
    if (!handler) return;
    dispatch_async(dispatch_get_main_queue(), ^{ handler(stage); });
}

- (void)emitProgressPercent:(double)percent fps:(double)fps eta:(double)eta analysisMode:(BOOL)analysisMode {
    BridgeProgressHandler handler = self.progressHandler;
    if (!handler) return;
    dispatch_async(dispatch_get_main_queue(), ^{ handler(percent, fps, eta, analysisMode); });
}

- (void)emitStatus:(NSString *)status {
    BridgeStatusHandler handler = self.statusHandler;
    if (!handler) return;
    dispatch_async(dispatch_get_main_queue(), ^{ handler(status); });
}

- (void)emitCompletion:(BOOL)success message:(NSString *)message {
    BridgeCompletionHandler handler = self.completionHandler;
    if (!handler) return;
    dispatch_async(dispatch_get_main_queue(), ^{ handler(success, message); });
}

@end
