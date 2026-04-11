#ifndef CONVERTER_BRIDGE_H
#define CONVERTER_BRIDGE_H

#import <Foundation/Foundation.h>
#import "converter.h"
#import "apple_m4v_creator.h"

NS_ASSUME_NONNULL_BEGIN

typedef void (^BridgeLogHandler)(NSString *line);
typedef void (^BridgeStageHandler)(NSString *stage);
typedef void (^BridgeProgressHandler)(double percent, double fps, double etaSeconds, BOOL analysisMode);
typedef void (^BridgeStatusHandler)(NSString *status);
typedef void (^BridgeCompletionHandler)(BOOL success, NSString *message);

@interface ConverterBridge : NSObject

- (NSString *)defaultOutputDirectory;
- (BOOL)ensureDefaultOutputDirectoryExists:(NSError * _Nullable * _Nullable)error;
- (ConvertOptions)makeOptionsWithCodec:(NSString *)codec
                                                             profile:(NSInteger)profile
                                                             deblock:(NSInteger)deblock
                             audioNorm:(NSString *)audioNorm
                         audioOutputMode:(NSString *)audioOutputMode
                          videoTrackPath:(NSString *)videoTrackPath
                                                                 genre:(NSInteger)genre
                             overwrite:(BOOL)overwrite
                             outputDir:(NSString *)outputDir;

- (BOOL)isRunning;
- (BOOL)isAppleM4VRunning;
- (void)startConversionWithOptions:(ConvertOptions)options
                                                         files:(NSArray<NSString *> *)files
                                                                log:(BridgeLogHandler)logHandler
                                                            stage:(BridgeStageHandler)stageHandler
                                                     progress:(BridgeProgressHandler)progressHandler
                                                         status:(BridgeStatusHandler)statusHandler
                                                 completion:(BridgeCompletionHandler)completionHandler;

- (void)startAppleM4VForFiles:(NSArray<NSString *> *)files
                     outputDir:(NSString *)outputDir
                     overwrite:(BOOL)overwrite
                                editBeforeMux:(BOOL)editBeforeMux
                                convertOptions:(ConvertOptions)convertOptions
                                    appleOptions:(AppleM4VOptions)appleOptions
                           log:(BridgeLogHandler)logHandler
                         stage:(BridgeStageHandler)stageHandler
                        status:(BridgeStatusHandler)statusHandler
                    completion:(BridgeCompletionHandler)completionHandler;

- (void)stopConversion;
- (void)stopAppleM4V;

@end

NS_ASSUME_NONNULL_END

#endif
