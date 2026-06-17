// fujify_macos.mm — native macOS file chooser (AppKit NSOpenPanel).
//
// Replaces the osascript dialog, which cold-starts the Script runtime (~2s on first use)
// and blocks. NSOpenPanel shows instantly and runs modeless via a completion handler, so
// the render loop never freezes. The handler fires on the main thread (the same thread as
// the UI), so the std::function can touch UI state directly — no locking needed.
#import <AppKit/AppKit.h>
#include <functional>
#include <string>
#include <vector>

// exts: lower-case extensions to allow (e.g. {"mp4","mov"}). Empty → any file.
// multi: allow selecting several files (for grid / compare). Calls done() on the main
// thread with the chosen paths (empty vector on Cancel / Esc).
void macos_choose_files(const std::function<void(std::vector<std::string>)>& done,
                        const std::vector<std::string>& exts, bool multi) {
    std::function<void(std::vector<std::string>)> cb = done;   // copied into the block
    NSMutableArray<NSString*>* types = nil;
    if (!exts.empty()) {
        types = [NSMutableArray arrayWithCapacity:exts.size()];
        for (const std::string& e : exts) [types addObject:[NSString stringWithUTF8String:e.c_str()]];
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        @autoreleasepool {
            NSOpenPanel* panel = [NSOpenPanel openPanel];
            panel.canChooseFiles = YES;
            panel.canChooseDirectories = NO;
            panel.allowsMultipleSelection = multi ? YES : NO;
            panel.title = multi ? @"Chọn nhiều file (grid)"
                                : (types ? @"Chọn video" : @"Chọn ảnh RAW / JPEG / video");
            if (types) {                                  // filter to the given extensions
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                panel.allowedFileTypes = types;           // simpler than UTType; fine on all target macOS
#pragma clang diagnostic pop
            }
            [NSApp activateIgnoringOtherApps:YES];        // bring chooser frontmost (Esc reaches it)
            [panel beginWithCompletionHandler:^(NSModalResponse r) {
                std::vector<std::string> paths;
                if (r == NSModalResponseOK)
                    for (NSURL* u in panel.URLs) paths.push_back(std::string(u.path.UTF8String));
                cb(paths);                                // empty on Cancel / Esc
            }];
        }
    });
}
