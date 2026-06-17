// fujify_macos.mm — native macOS file chooser (AppKit NSOpenPanel).
//
// Replaces the osascript dialog, which cold-starts the Script runtime (~2s on first use)
// and blocks. NSOpenPanel shows instantly and runs modeless via a completion handler, so
// the render loop never freezes. The handler fires on the main thread (the same thread as
// the UI), so the std::function can touch UI state directly — no locking needed.
#import <AppKit/AppKit.h>
#include <functional>
#include <string>

void macos_choose_file(const std::function<void(std::string)>& done) {
    std::function<void(std::string)> cb = done;          // copied into the block (heap-promoted)
    dispatch_async(dispatch_get_main_queue(), ^{
        @autoreleasepool {
            NSOpenPanel* panel = [NSOpenPanel openPanel];
            panel.canChooseFiles = YES;
            panel.canChooseDirectories = NO;
            panel.allowsMultipleSelection = NO;
            panel.title = @"Chọn ảnh RAW / JPEG / video";
            [NSApp activateIgnoringOtherApps:YES];        // bring chooser frontmost (Esc reaches it)
            [panel beginWithCompletionHandler:^(NSModalResponse r) {
                std::string path;
                if (r == NSModalResponseOK && panel.URLs.count > 0)
                    path = std::string(panel.URLs.firstObject.path.UTF8String);
                cb(path);                                 // empty on Cancel / Esc
            }];
        }
    });
}
