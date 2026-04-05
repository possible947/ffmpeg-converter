#import <Cocoa/Cocoa.h>
#import "converter_bridge.h"
#include <string.h>

typedef void (^DropPathsHandler)(NSArray<NSString *> *paths);

@interface DropWindow : NSWindow
@property (copy, nonatomic) DropPathsHandler dropHandler;
@property (assign, nonatomic) BOOL dropEnabled;
@end

@implementation DropWindow

- (instancetype)initWithContentRect:(NSRect)contentRect
                          styleMask:(NSWindowStyleMask)style
                            backing:(NSBackingStoreType)bufferingType
                              defer:(BOOL)flag {
    self = [super initWithContentRect:contentRect styleMask:style backing:bufferingType defer:flag];
    if (self) {
        _dropEnabled = YES;
        [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    }
    return self;
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    (void)sender;
    return self.dropEnabled ? NSDragOperationCopy : NSDragOperationNone;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    if (!self.dropEnabled || !self.dropHandler) {
        return NO;
    }

    NSPasteboard *pb = [sender draggingPasteboard];
    NSArray<NSURL *> *urls = [pb readObjectsForClasses:@[[NSURL class]]
                                                options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
    if (urls.count == 0) {
        return NO;
    }

    NSMutableArray<NSString *> *paths = [[NSMutableArray alloc] init];
    NSFileManager *fm = [NSFileManager defaultManager];
    for (NSURL *url in urls) {
        if (!url.fileURL) {
            continue;
        }
        NSString *path = url.path;
        BOOL isDir = NO;
        if ([fm fileExistsAtPath:path isDirectory:&isDir] && !isDir) {
            [paths addObject:path];
        }
    }

    if (paths.count == 0) {
        return NO;
    }

    self.dropHandler(paths);
    return YES;
}

@end

@interface AppDelegate : NSObject <NSApplicationDelegate, NSTableViewDataSource, NSTableViewDelegate>
@property (strong, nonatomic) NSWindow *window;
@property (strong, nonatomic) ConverterBridge *bridge;
@property (strong, nonatomic) NSTextField *outputLabel;
@property (strong, nonatomic) NSPopUpButton *codecPopup;
@property (strong, nonatomic) NSPopUpButton *profilePopup;
@property (strong, nonatomic) NSPopUpButton *deblockPopup;
@property (strong, nonatomic) NSPopUpButton *audioPopup;
@property (strong, nonatomic) NSPopUpButton *genrePopup;
@property (strong, nonatomic) NSButton *overwriteCheck;
@property (strong, nonatomic) NSButton *m4vEditCheck;
@property (strong, nonatomic) NSTextView *logView;
@property (strong, nonatomic) NSTextField *statusLabel;
@property (strong, nonatomic) NSProgressIndicator *progress;
@property (strong, nonatomic) NSButton *startButton;
@property (strong, nonatomic) NSButton *stopButton;
@property (strong, nonatomic) NSButton *appleM4VButton;
@property (strong, nonatomic) NSTableView *tableView;
@property (strong, nonatomic) NSMutableArray<NSString *> *filePaths;
@property (strong, nonatomic) NSButton *chooseOutputButton;
@property (strong, nonatomic) NSButton *addFilesButton;
@property (strong, nonatomic) NSButton *removeButton;
@property (strong, nonatomic) NSButton *clearButton;
@property (assign, nonatomic) BOOL terminateAfterStop;
 - (void)addInputPaths:(NSArray<NSString *> *)paths;
 - (BOOL)promptAppleM4VOptions:(AppleM4VOptions *)options;
@end

@implementation AppDelegate

static BOOL parseStrictInteger(NSString *text, NSInteger *outValue) {
    if (!outValue) {
        return NO;
    }

    NSString *trimmed = [text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (trimmed.length == 0) {
        return NO;
    }

    NSScanner *scanner = [NSScanner scannerWithString:trimmed];
    NSInteger value = 0;
    if (![scanner scanInteger:&value] || !scanner.isAtEnd) {
        return NO;
    }

    *outValue = value;
    return YES;
}

- (void)setRunningUIState:(BOOL)running {
    [self.startButton setEnabled:!running];
    [self.stopButton setEnabled:running];
    [self.appleM4VButton setEnabled:!running];

    [self.codecPopup setEnabled:!running];
    [self.audioPopup setEnabled:!running];
    [self.overwriteCheck setEnabled:!running];
    [self.m4vEditCheck setEnabled:!running];
    [self.chooseOutputButton setEnabled:!running];
    [self.addFilesButton setEnabled:!running];
    [self.removeButton setEnabled:!running];
    [self.clearButton setEnabled:!running];
    [self.tableView setEnabled:!running];
    [(DropWindow *)self.window setDropEnabled:!running];

    if (!running) {
        [self updateDependentControls];
    } else {
        [self.profilePopup setEnabled:NO];
        [self.deblockPopup setEnabled:NO];
        [self.genrePopup setEnabled:NO];
    }
}

- (void)setAppleM4VUIState:(BOOL)running {
    [self.startButton setEnabled:!running];
    [self.stopButton setEnabled:running];
    [self.appleM4VButton setEnabled:!running];

    [self.codecPopup setEnabled:!running];
    [self.audioPopup setEnabled:!running];
    [self.overwriteCheck setEnabled:!running];
    [self.m4vEditCheck setEnabled:!running];
    [self.chooseOutputButton setEnabled:!running];
    [self.addFilesButton setEnabled:!running];
    [self.removeButton setEnabled:!running];
    [self.clearButton setEnabled:!running];
    [self.tableView setEnabled:!running];
    [(DropWindow *)self.window setDropEnabled:!running];

    if (!running) {
        [self updateDependentControls];
    } else {
        [self.profilePopup setEnabled:NO];
        [self.deblockPopup setEnabled:NO];
        [self.genrePopup setEnabled:NO];
    }
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;

    self.bridge = [[ConverterBridge alloc] init];
    self.filePaths = [[NSMutableArray alloc] init];
    NSError *dirError = nil;
    [self.bridge ensureDefaultOutputDirectoryExists:&dirError];

    NSRect frame = NSMakeRect(0, 0, 800, 600);
    NSUInteger style = NSWindowStyleMaskTitled |
                       NSWindowStyleMaskClosable |
                       NSWindowStyleMaskMiniaturizable;

        self.window = [[DropWindow alloc] initWithContentRect:frame
                                                                                                 styleMask:style
                                                                                                     backing:NSBackingStoreBuffered
                                                                                                         defer:NO];
        [self.window setMinSize:frame.size];
        [self.window setMaxSize:frame.size];
    [self.window setTitle:@"ffmpeg-converter GUI (macOS Native - WIP)"];
    [self.window center];

    __weak typeof(self) weakSelf = self;
    [(DropWindow *)self.window setDropHandler:^(NSArray<NSString *> *paths) {
        [weakSelf addInputPaths:paths];
    }];

    NSView *content = [self.window contentView];

    NSTextField *codecLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(16, 556, 60, 24)];
    [codecLabel setStringValue:@"Codec:"];
    [codecLabel setBezeled:NO];
    [codecLabel setEditable:NO];
    [codecLabel setDrawsBackground:NO];
    [content addSubview:codecLabel];

    self.codecPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(82, 554, 160, 28) pullsDown:NO];
    [self.codecPopup addItemsWithTitles:@[@"copy", @"prores", @"prores_ks", @"prores_videotoolbox", @"hevc_videotoolbox"]];
    [self.codecPopup setTarget:self];
    [self.codecPopup setAction:@selector(onCodecChanged:)];
    [content addSubview:self.codecPopup];

    NSTextField *profileLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(16, 522, 60, 24)];
    [profileLabel setStringValue:@"Profile:"];
    [profileLabel setBezeled:NO];
    [profileLabel setEditable:NO];
    [profileLabel setDrawsBackground:NO];
    [content addSubview:profileLabel];

    self.profilePopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(82, 520, 150, 28) pullsDown:NO];
    [self.profilePopup addItemsWithTitles:@[@"lt", @"standard", @"hq", @"4444"]];
    [self.profilePopup selectItemAtIndex:1];
    [content addSubview:self.profilePopup];

    NSTextField *deblockLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(248, 522, 80, 24)];
    [deblockLabel setStringValue:@"Deblock:"];
    [deblockLabel setBezeled:NO];
    [deblockLabel setEditable:NO];
    [deblockLabel setDrawsBackground:NO];
    [content addSubview:deblockLabel];

    self.deblockPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(332, 520, 180, 28) pullsDown:NO];
    [self.deblockPopup addItemsWithTitles:@[@"none", @"weak", @"strong"]];
    [self.deblockPopup selectItemAtIndex:0];
    [content addSubview:self.deblockPopup];

    NSTextField *audioLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(248, 556, 80, 24)];
    [audioLabel setStringValue:@"Audio norm:"];
    [audioLabel setBezeled:NO];
    [audioLabel setEditable:NO];
    [audioLabel setDrawsBackground:NO];
    [content addSubview:audioLabel];

    self.audioPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(332, 554, 180, 28) pullsDown:NO];
    [self.audioPopup addItemsWithTitles:@[@"none", @"peak_norm", @"peak_norm_2pass", @"loudness_norm", @"loudness_norm_2pass"]];
    [self.audioPopup setTarget:self];
    [self.audioPopup setAction:@selector(onAudioNormChanged:)];
    [content addSubview:self.audioPopup];

    NSTextField *genreLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(524, 556, 54, 24)];
    [genreLabel setStringValue:@"Genre:"];
    [genreLabel setBezeled:NO];
    [genreLabel setEditable:NO];
    [genreLabel setDrawsBackground:NO];
    [content addSubview:genreLabel];

    self.genrePopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(582, 554, 200, 28) pullsDown:NO];
    [self.genrePopup addItemsWithTitles:@[@"edm", @"rock", @"hiphop", @"classical", @"podcast"]];
    [self.genrePopup selectItemAtIndex:0];
    [content addSubview:self.genrePopup];

    self.overwriteCheck = [[NSButton alloc] initWithFrame:NSMakeRect(524, 522, 260, 24)];
    [self.overwriteCheck setButtonType:NSButtonTypeSwitch];
    [self.overwriteCheck setTitle:@"Overwrite existing files"];
    [self.overwriteCheck setState:NSControlStateValueOff];
    [content addSubview:self.overwriteCheck];

    self.m4vEditCheck = [[NSButton alloc] initWithFrame:NSMakeRect(524, 500, 260, 20)];
    [self.m4vEditCheck setButtonType:NSButtonTypeSwitch];
    [self.m4vEditCheck setTitle:@"m4v edit (main -> m4v)"];
    [self.m4vEditCheck setState:NSControlStateValueOff];
    [content addSubview:self.m4vEditCheck];

    NSString *defaultOutput = [self.bridge defaultOutputDirectory];

    NSTextField *outputLabelTitle = [[NSTextField alloc] initWithFrame:NSMakeRect(16, 490, 74, 24)];
    [outputLabelTitle setStringValue:@"Output dir:"];
    [outputLabelTitle setBezeled:NO];
    [outputLabelTitle setEditable:NO];
    [outputLabelTitle setDrawsBackground:NO];
    [content addSubview:outputLabelTitle];

    self.outputLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(94, 490, 586, 24)];
    [self.outputLabel setStringValue:defaultOutput];
    [self.outputLabel setBezeled:NO];
    [self.outputLabel setEditable:NO];
    [self.outputLabel setDrawsBackground:NO];
    [content addSubview:self.outputLabel];

    self.chooseOutputButton = [[NSButton alloc] initWithFrame:NSMakeRect(684, 486, 98, 30)];
    [self.chooseOutputButton setTitle:@"Choose..."];
    [self.chooseOutputButton setBezelStyle:NSBezelStyleRounded];
    [self.chooseOutputButton setTarget:self];
    [self.chooseOutputButton setAction:@selector(onChooseOutputClicked:)];
    [content addSubview:self.chooseOutputButton];

    self.addFilesButton = [[NSButton alloc] initWithFrame:NSMakeRect(16, 456, 110, 30)];
    [self.addFilesButton setTitle:@"Add files..."];
    [self.addFilesButton setBezelStyle:NSBezelStyleRounded];
    [self.addFilesButton setTarget:self];
    [self.addFilesButton setAction:@selector(onAddFilesClicked:)];
    [content addSubview:self.addFilesButton];

    self.removeButton = [[NSButton alloc] initWithFrame:NSMakeRect(132, 456, 128, 30)];
    [self.removeButton setTitle:@"Remove selected"];
    [self.removeButton setBezelStyle:NSBezelStyleRounded];
    [self.removeButton setTarget:self];
    [self.removeButton setAction:@selector(onRemoveSelectedClicked:)];
    [content addSubview:self.removeButton];

    self.clearButton = [[NSButton alloc] initWithFrame:NSMakeRect(266, 456, 94, 30)];
    [self.clearButton setTitle:@"Clear list"];
    [self.clearButton setBezelStyle:NSBezelStyleRounded];
    [self.clearButton setTarget:self];
    [self.clearButton setAction:@selector(onClearListClicked:)];
    [content addSubview:self.clearButton];

    NSScrollView *fileListScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(16, 218, 766, 230)];
    self.tableView = [[NSTableView alloc] initWithFrame:[fileListScroll bounds]];
    NSTableColumn *col = [[NSTableColumn alloc] initWithIdentifier:@"file"];
    [col setTitle:@"Input files"];
    [col setWidth:748];
    [self.tableView addTableColumn:col];
    [self.tableView setHeaderView:nil];
    [self.tableView setDataSource:self];
    [self.tableView setDelegate:self];
    [fileListScroll setDocumentView:self.tableView];
    [fileListScroll setHasVerticalScroller:YES];
    [content addSubview:fileListScroll];

    self.startButton = [[NSButton alloc] initWithFrame:NSMakeRect(16, 182, 96, 32)];
    [self.startButton setTitle:@"Start"];
    [self.startButton setBezelStyle:NSBezelStyleRounded];
    [self.startButton setTarget:self];
    [self.startButton setAction:@selector(onStartClicked:)];
    [content addSubview:self.startButton];

    self.stopButton = [[NSButton alloc] initWithFrame:NSMakeRect(120, 182, 96, 32)];
    [self.stopButton setTitle:@"Stop"];
    [self.stopButton setBezelStyle:NSBezelStyleRounded];
    [self.stopButton setEnabled:NO];
    [self.stopButton setTarget:self];
    [self.stopButton setAction:@selector(onStopClicked:)];
    [content addSubview:self.stopButton];

    self.appleM4VButton = [[NSButton alloc] initWithFrame:NSMakeRect(224, 182, 160, 32)];
    [self.appleM4VButton setTitle:@"Apple m4v creator"];
    [self.appleM4VButton setBezelStyle:NSBezelStyleRounded];
    [self.appleM4VButton setTarget:self];
    [self.appleM4VButton setAction:@selector(onAppleM4VClicked:)];
    [content addSubview:self.appleM4VButton];

    self.progress = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(16, 154, 766, 20)];
    [self.progress setIndeterminate:NO];
    [self.progress setMinValue:0.0];
    [self.progress setMaxValue:100.0];
    [self.progress setDoubleValue:0.0];
    [content addSubview:self.progress];

    NSScrollView *logScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(16, 42, 766, 102)];
    self.logView = [[NSTextView alloc] initWithFrame:[logScroll bounds]];
    [self.logView setEditable:NO];
    if (dirError) {
        [self.logView setString:[NSString stringWithFormat:@"Native macOS UI skeleton initialized.\nDefault output dir error: %@\n", dirError.localizedDescription]];
    } else {
        [self.logView setString:@"Native macOS UI skeleton initialized.\n"];
    }
    [logScroll setDocumentView:self.logView];
    [logScroll setHasVerticalScroller:YES];
    [content addSubview:logScroll];

    self.statusLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(16, 12, 766, 24)];
    [self.statusLabel setStringValue:@"Ready"];
    [self.statusLabel setBezeled:NO];
    [self.statusLabel setEditable:NO];
    [self.statusLabel setDrawsBackground:NO];
    [content addSubview:self.statusLabel];

    [self updateDependentControls];

    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)onStartClicked:(id)sender {
    (void)sender;

    if (self.filePaths.count == 0) {
        [self appendLogLine:@"No input files selected"];
        [self.statusLabel setStringValue:@"No input files selected"];
        return;
    }

    if ([self.bridge isRunning] || [self.bridge isAppleM4VRunning]) {
        [self appendLogLine:@"Another job is already running"];
        return;
    }

    NSString *codec = self.codecPopup.titleOfSelectedItem ?: @"";
    NSInteger profile = self.profilePopup.isEnabled ? (NSInteger)self.profilePopup.indexOfSelectedItem + 1 : 0;
    NSInteger deblock = self.deblockPopup.isEnabled ? (NSInteger)self.deblockPopup.indexOfSelectedItem + 1 : 0;
    NSString *audioNorm = self.audioPopup.titleOfSelectedItem ?: @"";
    NSInteger genre = self.genrePopup.isEnabled ? (NSInteger)self.genrePopup.indexOfSelectedItem + 1 : 0;
    BOOL overwrite = (self.overwriteCheck.state == NSControlStateValueOn);
    NSString *outputDir = self.outputLabel.stringValue ?: @"";

    if (outputDir.length == 0) {
        outputDir = [self.bridge defaultOutputDirectory];
        [self.outputLabel setStringValue:outputDir];
    }

    NSError *dirError = nil;
    BOOL outputOk = [[NSFileManager defaultManager] createDirectoryAtPath:outputDir
                                              withIntermediateDirectories:YES
                                                               attributes:nil
                                                                    error:&dirError];
    if (!outputOk) {
        NSString *msg = [NSString stringWithFormat:@"Output dir error: %@", dirError.localizedDescription ?: @"unknown"];
        [self appendLogLine:msg];
        [self.statusLabel setStringValue:msg];
        return;
    }

    ConvertOptions opts = [self.bridge makeOptionsWithCodec:codec
                                                    profile:profile
                                                    deblock:deblock
                                                  audioNorm:audioNorm
                                                      genre:genre
                                                  overwrite:overwrite
                                                  outputDir:outputDir];

    [self setRunningUIState:YES];
    [self.progress setDoubleValue:0.0];
    [self.statusLabel setStringValue:@"Starting..."];
    [self appendLogLine:@"Conversion started"];

    __weak typeof(self) weakSelf = self;
    [self.bridge startConversionWithOptions:opts
                                      files:[self.filePaths copy]
                                         log:^(NSString *line) {
                                             [weakSelf appendLogLine:line];
                                         }
                                       stage:^(NSString *stage) {
                                           [weakSelf.statusLabel setStringValue:[NSString stringWithFormat:@"Stage: %@", stage]];
                                       }
                                    progress:^(double percent, double fps, double etaSeconds, BOOL analysisMode) {
                                        [weakSelf.progress setDoubleValue:percent];
                                        if (analysisMode) {
                                            [weakSelf.statusLabel setStringValue:[NSString stringWithFormat:@"Analysis %.0f%% ETA %.0fs", percent, etaSeconds]];
                                        } else {
                                            [weakSelf.statusLabel setStringValue:[NSString stringWithFormat:@"Encoding %.0f%% %.0f fps ETA %.0fs", percent, fps, etaSeconds]];
                                        }
                                    }
                                      status:^(NSString *status) {
                                          [weakSelf.statusLabel setStringValue:status];
                                      }
                                  completion:^(BOOL success, NSString *message) {
                                      [weakSelf setRunningUIState:NO];
                                      if (success) {
                                          [weakSelf.progress setDoubleValue:100.0];
                                          [weakSelf.statusLabel setStringValue:@"Completed"];
                                          [weakSelf appendLogLine:@"Conversion completed"];
                                          [weakSelf.filePaths removeAllObjects];
                                          [weakSelf.tableView reloadData];
                                      } else if ([message isEqualToString:@"Stopped"]) {
                                          [weakSelf.statusLabel setStringValue:@"Stopped"];
                                          [weakSelf appendLogLine:@"Conversion stopped"];
                                      } else {
                                          [weakSelf.statusLabel setStringValue:[NSString stringWithFormat:@"Finished: %@", message]];
                                          [weakSelf appendLogLine:[NSString stringWithFormat:@"Finished with errors: %@", message]];
                                      }

                                      if (weakSelf.terminateAfterStop) {
                                          weakSelf.terminateAfterStop = NO;
                                          [NSApp terminate:nil];
                                      }
                                  }];
}

- (void)onStopClicked:(id)sender {
    (void)sender;
    if ([self.bridge isRunning]) {
        [self.bridge stopConversion];
        [self appendLogLine:@"Stop requested"];
        [self.statusLabel setStringValue:@"Stopping..."];
        return;
    }

    if ([self.bridge isAppleM4VRunning]) {
        [self.bridge stopAppleM4V];
        [self appendLogLine:@"Apple m4v stop requested"];
        [self.statusLabel setStringValue:@"Apple m4v stopping..."];
    }
}

- (void)onAppleM4VClicked:(id)sender {
    (void)sender;

    if (self.filePaths.count == 0) {
        [self appendLogLine:@"No input files selected"];
        [self.statusLabel setStringValue:@"No input files selected"];
        return;
    }

    if ([self.bridge isRunning] || [self.bridge isAppleM4VRunning]) {
        [self appendLogLine:@"Another job is already running"];
        [self.statusLabel setStringValue:@"Another job is already running"];
        return;
    }

    BOOL overwrite = (self.overwriteCheck.state == NSControlStateValueOn);
    BOOL editBeforeMux = (self.m4vEditCheck.state == NSControlStateValueOn);
    NSString *outputDir = self.outputLabel.stringValue ?: @"";
    if (outputDir.length == 0) {
        outputDir = [self.bridge defaultOutputDirectory];
        [self.outputLabel setStringValue:outputDir];
    }

    NSError *dirError = nil;
    BOOL outputOk = [[NSFileManager defaultManager] createDirectoryAtPath:outputDir
                                              withIntermediateDirectories:YES
                                                               attributes:nil
                                                                    error:&dirError];
    if (!outputOk) {
        NSString *msg = [NSString stringWithFormat:@"Output dir error: %@", dirError.localizedDescription ?: @"unknown"];
        [self appendLogLine:msg];
        [self.statusLabel setStringValue:msg];
        return;
    }

    AppleM4VOptions appleOptions = AppleM4VDefaultOptions();
    if (![self promptAppleM4VOptions:&appleOptions]) {
        [self appendLogLine:@"Apple m4v creator cancelled by user"];
        [self.statusLabel setStringValue:@"Ready"];
        return;
    }

    NSString *codec = self.codecPopup.titleOfSelectedItem ?: @"";
    NSInteger profile = self.profilePopup.isEnabled ? (NSInteger)self.profilePopup.indexOfSelectedItem + 1 : 0;
    NSInteger deblock = self.deblockPopup.isEnabled ? (NSInteger)self.deblockPopup.indexOfSelectedItem + 1 : 0;
    NSString *audioNorm = self.audioPopup.titleOfSelectedItem ?: @"";
    NSInteger genre = self.genrePopup.isEnabled ? (NSInteger)self.genrePopup.indexOfSelectedItem + 1 : 0;

    ConvertOptions convertOptions = [self.bridge makeOptionsWithCodec:codec
                                                               profile:profile
                                                               deblock:deblock
                                                             audioNorm:audioNorm
                                                                 genre:genre
                                                             overwrite:overwrite
                                                             outputDir:outputDir];

    [self setAppleM4VUIState:YES];
    [self.progress setDoubleValue:0.0];
    [self.statusLabel setStringValue:@"Apple m4v creator: starting..."];
    [self appendLogLine:editBeforeMux ? @"Apple m4v creator started (edit-before-mux mode)" : @"Apple m4v creator started (direct mode)"];

    __weak typeof(self) weakSelf = self;
    [self.bridge startAppleM4VForFiles:[self.filePaths copy]
                             outputDir:outputDir
                             overwrite:overwrite
                         editBeforeMux:editBeforeMux
                         convertOptions:convertOptions
                           appleOptions:appleOptions
                                   log:^(NSString *line) {
                                       [weakSelf appendLogLine:line];
                                   }
                                 stage:^(NSString *stage) {
                                     [weakSelf.statusLabel setStringValue:[NSString stringWithFormat:@"Stage: %@", stage]];
                                 }
                                status:^(NSString *status) {
                                    [weakSelf.statusLabel setStringValue:status];
                                }
                            completion:^(BOOL success, NSString *message) {
                                [weakSelf setAppleM4VUIState:NO];
                                if (success) {
                                    [weakSelf.progress setDoubleValue:100.0];
                                    [weakSelf.statusLabel setStringValue:@"Apple m4v completed"];
                                    [weakSelf appendLogLine:message ?: @"Apple m4v completed"];
                                } else if ([message isEqualToString:@"Stopped"]) {
                                    [weakSelf.statusLabel setStringValue:@"Apple m4v stopped"];
                                    [weakSelf appendLogLine:@"Apple m4v stopped"];
                                } else {
                                    [weakSelf.statusLabel setStringValue:[NSString stringWithFormat:@"Apple m4v finished: %@", message ?: @"unknown"]];
                                    [weakSelf appendLogLine:[NSString stringWithFormat:@"Apple m4v finished with errors: %@", message ?: @"unknown"]];
                                }

                                if (weakSelf.terminateAfterStop) {
                                    weakSelf.terminateAfterStop = NO;
                                    [NSApp terminate:nil];
                                }
                            }];
}

- (BOOL)promptAppleM4VOptions:(AppleM4VOptions *)options {
    if (!options) {
        return NO;
    }

    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"Apple m4v creator options";
    alert.informativeText = @"Set track and audio parameters for Apple M4V mux.";
    [alert addButtonWithTitle:@"Start"];
    [alert addButtonWithTitle:@"Cancel"];

    NSView *container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 360, 170)];

    NSArray<NSString *> *labels = @[
        @"Video track index:",
        @"Audio track index:",
        @"AAC quality (1..9):",
        @"AC3 bitrate kbps:",
        @"Audio language:",
    ];

    NSMutableArray<NSTextField *> *fields = [[NSMutableArray alloc] init];
    NSArray<NSString *> *defaults = @[
        [NSString stringWithFormat:@"%ld", (long)options->videoTrackIndex],
        [NSString stringWithFormat:@"%ld", (long)options->audioTrackIndex],
        [NSString stringWithFormat:@"%ld", (long)options->aacQuality],
        [NSString stringWithFormat:@"%ld", (long)options->ac3BitrateKbps],
        options->audioLang[0] != '\0' ? [NSString stringWithUTF8String:options->audioLang] : @"rus"
    ];

    CGFloat y = 138;
    for (NSInteger i = 0; i < (NSInteger)labels.count; i++) {
        NSTextField *label = [[NSTextField alloc] initWithFrame:NSMakeRect(0, y, 170, 22)];
        label.stringValue = labels[(NSUInteger)i];
        label.bezeled = NO;
        label.editable = NO;
        label.drawsBackground = NO;
        [container addSubview:label];

        NSTextField *input = [[NSTextField alloc] initWithFrame:NSMakeRect(178, y - 1, 180, 24)];
        input.stringValue = defaults[(NSUInteger)i];
        [container addSubview:input];
        [fields addObject:input];

        y -= 28;
    }

    NSButton *chaptersCheck = [[NSButton alloc] initWithFrame:NSMakeRect(0, 2, 240, 22)];
    [chaptersCheck setButtonType:NSButtonTypeSwitch];
    [chaptersCheck setTitle:@"Import chapters"];
    [chaptersCheck setState:options->addChapters ? NSControlStateValueOn : NSControlStateValueOff];
    [container addSubview:chaptersCheck];

    alert.accessoryView = container;

    NSModalResponse response = [alert runModal];
    if (response != NSAlertFirstButtonReturn) {
        return NO;
    }

    NSInteger vIndex = 0;
    NSInteger aIndex = 0;
    NSInteger aacQ = 0;
    NSInteger ac3 = 0;

    if (!parseStrictInteger(fields[0].stringValue, &vIndex)) {
        [self appendLogLine:@"Apple m4v options error: invalid video track index"];
        [self.statusLabel setStringValue:@"Apple m4v options invalid"];
        return NO;
    }
    if (!parseStrictInteger(fields[1].stringValue, &aIndex)) {
        [self appendLogLine:@"Apple m4v options error: invalid audio track index"];
        [self.statusLabel setStringValue:@"Apple m4v options invalid"];
        return NO;
    }
    if (!parseStrictInteger(fields[2].stringValue, &aacQ)) {
        [self appendLogLine:@"Apple m4v options error: invalid AAC quality"];
        [self.statusLabel setStringValue:@"Apple m4v options invalid"];
        return NO;
    }
    if (!parseStrictInteger(fields[3].stringValue, &ac3)) {
        [self appendLogLine:@"Apple m4v options error: invalid AC3 bitrate"];
        [self.statusLabel setStringValue:@"Apple m4v options invalid"];
        return NO;
    }
    NSString *lang = [fields[4].stringValue.lowercaseString stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];

    if (vIndex < 0 || aIndex < 0) {
        [self appendLogLine:@"Apple m4v options error: track index must be >= 0"];
        [self.statusLabel setStringValue:@"Apple m4v options invalid"];
        return NO;
    }
    if (aacQ < 1 || aacQ > 9) {
        [self appendLogLine:@"Apple m4v options error: AAC quality must be 1..9"];
        [self.statusLabel setStringValue:@"Apple m4v options invalid"];
        return NO;
    }
    if (ac3 < 96) {
        [self appendLogLine:@"Apple m4v options error: AC3 bitrate must be >= 96 kbps"];
        [self.statusLabel setStringValue:@"Apple m4v options invalid"];
        return NO;
    }
    if (lang.length == 0) {
        [self appendLogLine:@"Apple m4v options error: audio language cannot be empty"];
        [self.statusLabel setStringValue:@"Apple m4v options invalid"];
        return NO;
    }

    options->videoTrackIndex = vIndex;
    options->audioTrackIndex = aIndex;
    options->aacQuality = aacQ;
    options->ac3BitrateKbps = ac3;
    memset(options->audioLang, 0, sizeof(options->audioLang));
    strncpy(options->audioLang, lang.UTF8String, sizeof(options->audioLang) - 1);
    options->addChapters = (chaptersCheck.state == NSControlStateValueOn);
    return YES;
}

- (void)onChooseOutputClicked:(id)sender {
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    [panel setCanChooseDirectories:YES];
    [panel setCanChooseFiles:NO];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanCreateDirectories:YES];

    if ([panel runModal] == NSModalResponseOK) {
        NSURL *url = panel.URL;
        if (url.path.length > 0) {
            [self.outputLabel setStringValue:url.path];
        }
    }
}

- (void)onAddFilesClicked:(id)sender {
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    [panel setCanChooseDirectories:NO];
    [panel setCanChooseFiles:YES];
    [panel setAllowsMultipleSelection:YES];

    if ([panel runModal] == NSModalResponseOK) {
        NSMutableArray<NSString *> *paths = [[NSMutableArray alloc] init];
        for (NSURL *url in panel.URLs) {
            if (url.path.length > 0) {
                [paths addObject:url.path];
            }
        }
        [self addInputPaths:paths];
    }
}

- (void)addInputPaths:(NSArray<NSString *> *)paths {
    if (paths.count == 0) {
        return;
    }

    NSUInteger added = 0;
    for (NSString *path in paths) {
        if (path.length == 0 || [self.filePaths containsObject:path]) {
            continue;
        }
        [self.filePaths addObject:path];
        added++;
    }

    if (added > 0) {
        [self.tableView reloadData];
        [self.statusLabel setStringValue:[NSString stringWithFormat:@"Added %lu file(s)", (unsigned long)added]];
    }
}

- (void)onRemoveSelectedClicked:(id)sender {
    (void)sender;
    NSInteger row = self.tableView.selectedRow;
    if (row >= 0 && row < (NSInteger)self.filePaths.count) {
        [self.filePaths removeObjectAtIndex:(NSUInteger)row];
        [self.tableView reloadData];
    }
}

- (void)onClearListClicked:(id)sender {
    (void)sender;
    [self.filePaths removeAllObjects];
    [self.tableView reloadData];
}

- (void)onCodecChanged:(id)sender {
    (void)sender;
    [self updateDependentControls];
}

- (void)onAudioNormChanged:(id)sender {
    (void)sender;
    [self updateDependentControls];
}

- (void)updateDependentControls {
    NSString *codec = self.codecPopup.titleOfSelectedItem ?: @"";
    // Profile: prores software and prores_videotoolbox hardware share same profiles
    BOOL profileEnabled = ([codec isEqualToString:@"prores"] ||
                           [codec isEqualToString:@"prores_ks"] ||
                           [codec isEqualToString:@"prores_videotoolbox"]);
    // Deblock: software prores encoders only; hardware encoders skip
    BOOL deblockEnabled = ([codec isEqualToString:@"prores"] ||
                           [codec isEqualToString:@"prores_ks"]);
    [self.profilePopup setEnabled:profileEnabled];
    [self.deblockPopup setEnabled:deblockEnabled];

    NSString *audioNorm = self.audioPopup.titleOfSelectedItem ?: @"";
    BOOL genreEnabled = [audioNorm isEqualToString:@"loudness_norm_2pass"];
    [self.genrePopup setEnabled:genreEnabled];
}

- (void)appendLogLine:(NSString *)line {
    if (!line) return;
    NSString *full = [line stringByAppendingString:@"\n"];
    NSTextStorage *storage = self.logView.textStorage;
    [storage appendAttributedString:[[NSAttributedString alloc] initWithString:full]];
    [self.logView scrollRangeToVisible:NSMakeRange(storage.length, 0)];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
    (void)tableView;
    return (NSInteger)self.filePaths.count;
}

- (id)tableView:(NSTableView *)tableView objectValueForTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row {
    (void)tableView;
    (void)tableColumn;
    if (row < 0 || row >= (NSInteger)self.filePaths.count) {
        return @"";
    }
    return self.filePaths[(NSUInteger)row];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    if ([self.bridge isRunning]) {
        self.terminateAfterStop = YES;
        [self.bridge stopConversion];
        return NO;
    }
    if ([self.bridge isAppleM4VRunning]) {
        self.terminateAfterStop = YES;
        [self.bridge stopAppleM4V];
        return NO;
    }
    return YES;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    (void)sender;
    if ([self.bridge isRunning]) {
        self.terminateAfterStop = YES;
        [self.bridge stopConversion];
        [self.statusLabel setStringValue:@"Stopping before quit..."];
        [self appendLogLine:@"Quit requested while conversion is running. Stop requested."];
        return NSTerminateCancel;
    }
    if ([self.bridge isAppleM4VRunning]) {
        self.terminateAfterStop = YES;
        [self.bridge stopAppleM4V];
        [self.statusLabel setStringValue:@"Stopping Apple m4v before quit..."];
        [self appendLogLine:@"Quit requested while Apple m4v is running. Stop requested."];
        return NSTerminateCancel;
    }
    return NSTerminateNow;
}

@end

int main(int argc, const char *argv[]) {
    (void)argc;
    (void)argv;

    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        AppDelegate *delegate = [[AppDelegate alloc] init];
        [app setDelegate:delegate];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app run];
    }

    return 0;
}
