#ifndef APPLE_M4V_CREATOR_H
#define APPLE_M4V_CREATOR_H

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef void (^AppleM4VLogHandler)(NSString *line);
typedef void (^AppleM4VStageHandler)(NSString *stage);

typedef struct {
    NSInteger videoTrackIndex;
    NSInteger audioTrackIndex;
    NSInteger ac3BitrateKbps;
    BOOL addChapters;
  char audioLang[16];
} AppleM4VOptions;

FOUNDATION_EXPORT AppleM4VOptions AppleM4VDefaultOptions(void);

@interface AppleM4VCreator : NSObject

+ (nullable NSString *)resolveBinaryWithPrimaryName:(NSString *)name
                                            envVars:(NSArray<NSString *> *)envVars
                                         candidates:(NSArray<NSString *> *)candidates;

- (instancetype)initWithFfmpegBin:(NSString *)ffmpegBin
                        ffprobeBin:(NSString *)ffprobeBin
                         mp4BoxBin:(NSString *)mp4BoxBin;

- (BOOL)createFromInput:(NSString *)inputFile
                 output:(NSString *)outputFile
                options:(AppleM4VOptions)options
               stopFlag:(volatile BOOL *)stopFlag
                    log:(nullable AppleM4VLogHandler)logHandler
                  stage:(nullable AppleM4VStageHandler)stageHandler
                  error:(NSString * _Nullable * _Nullable)errorText;

@end

NS_ASSUME_NONNULL_END

#endif
