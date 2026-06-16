#import "RNExifOverlay.h"
#import <UIKit/UIKit.h>

@implementation RNExifOverlay

RCT_EXPORT_MODULE();

+ (BOOL)requiresMainQueueSetup { return NO; }

RCT_EXPORT_METHOD(render:(NSString *)imageUri
                  lineCamera:(NSString *)lineCamera
                  lineExposure:(NSString *)lineExposure
                  resolver:(RCTPromiseResolveBlock)resolve
                  rejecter:(RCTPromiseRejectBlock)reject)
{
  dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
    NSString *path = [imageUri hasPrefix:@"file://"]
      ? [imageUri substringFromIndex:7]
      : imageUri;

    UIImage *src = [UIImage imageWithContentsOfFile:path];
    if (!src) {
      reject(@"load_err", @"Cannot load image", nil);
      return;
    }

    CGFloat w = src.size.width;
    CGFloat h = src.size.height;
    CGFloat bar = floor(w * 0.13);
    CGFloat pad = floor(w * 0.05);

    UIGraphicsBeginImageContextWithOptions(CGSizeMake(w, h + bar), YES, 1.0);

    // Photo
    [src drawInRect:CGRectMake(0, 0, w, h)];

    // Black bar
    UIColor *barColor = [UIColor colorWithRed:0.039 green:0.039 blue:0.039 alpha:1.0];
    [barColor setFill];
    UIRectFill(CGRectMake(0, h, w, bar));

    // Camera line
    if (lineCamera.length > 0) {
      NSDictionary *attrs = @{
        NSFontAttributeName: [UIFont systemFontOfSize:w * 0.032 weight:UIFontWeightSemibold],
        NSForegroundColorAttributeName: [UIColor colorWithRed:0.91 green:0.878 blue:0.831 alpha:1.0],
      };
      [lineCamera drawAtPoint:CGPointMake(pad, h + bar * 0.18) withAttributes:attrs];
    }

    // Exposure line
    if (lineExposure.length > 0) {
      NSDictionary *attrs = @{
        NSFontAttributeName: [UIFont systemFontOfSize:w * 0.026 weight:UIFontWeightRegular],
        NSForegroundColorAttributeName: [UIColor colorWithRed:0.784 green:0.663 blue:0.431 alpha:1.0],
      };
      [lineExposure drawAtPoint:CGPointMake(pad, h + bar * 0.56) withAttributes:attrs];
    }

    // Brand
    NSDictionary *brandAttrs = @{
      NSFontAttributeName: [UIFont italicSystemFontOfSize:w * 0.026],
      NSForegroundColorAttributeName: [UIColor colorWithRed:0.353 green:0.318 blue:0.251 alpha:1.0],
    };
    CGSize brandSize = [@"Fujify" sizeWithAttributes:brandAttrs];
    [@"Fujify" drawAtPoint:CGPointMake(w - pad - brandSize.width, h + (bar - brandSize.height) / 2.0)
            withAttributes:brandAttrs];

    UIImage *result = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();

    NSData *jpeg = UIImageJPEGRepresentation(result, 1.0);
    NSString *out = [NSTemporaryDirectory()
      stringByAppendingPathComponent:[NSString stringWithFormat:@"exif_%@.jpg",
        [[NSUUID UUID] UUIDString]]];
    [jpeg writeToFile:out atomically:YES];

    resolve([NSString stringWithFormat:@"file://%@", out]);
  });
}

@end
