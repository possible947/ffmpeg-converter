#import "converter_bridge.h"
#include <string.h>
#include <stdlib.h>

static __weak id s_activeBridge;

@interface ConverterBridge () {
    Converter *_converter;
    BOOL _running;
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

- (void)configureBundledToolsEnvironment {
    NSString *resourcePath = [[NSBundle mainBundle] resourcePath];
    if (resourcePath.length == 0) {
        return;
    }

    NSString *binDir = [resourcePath stringByAppendingPathComponent:@"bin"];
    NSString *ffmpegPath = [binDir stringByAppendingPathComponent:@"ffmpeg"];
    NSString *ffprobePath = [binDir stringByAppendingPathComponent:@"ffprobe"];

    NSFileManager *fm = [NSFileManager defaultManager];
    BOOL hasFfmpeg = [fm isExecutableFileAtPath:ffmpegPath];
    BOOL hasFfprobe = [fm isExecutableFileAtPath:ffprobePath];

    if (hasFfmpeg) {
        setenv("FFMPEG", ffmpegPath.UTF8String, 1);
        setenv("FFMPEG_BIN", ffmpegPath.UTF8String, 1);
    }
    if (hasFfprobe) {
        setenv("FFPROBE", ffprobePath.UTF8String, 1);
        setenv("FFPROBE_BIN", ffprobePath.UTF8String, 1);
    }

    if (hasFfmpeg || hasFfprobe) {
        const char *pathEnv = getenv("PATH");
        NSString *currentPath = pathEnv ? [NSString stringWithUTF8String:pathEnv] : @"";
        if ([currentPath rangeOfString:binDir].location == NSNotFound) {
            NSString *updated = currentPath.length > 0 ? [NSString stringWithFormat:@"%@:%@", binDir, currentPath]
                                                      : binDir;
            setenv("PATH", updated.UTF8String, 1);
        }
    }
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
        if (_running) {
            if (completionHandler) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    completionHandler(NO, @"Conversion is already running");
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
        converter_set_options(c, &optsCopy);
        ConverterError err = converter_process_files(c, (const char **)cFiles, fileCount);

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

- (void)stopConversion {
    @synchronized (self) {
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
