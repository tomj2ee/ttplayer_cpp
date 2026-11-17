#include "app/PlatformMac.hpp"

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#import <CoreText/CoreText.h>

#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

@interface TTPlayerReopenHandler : NSObject
@property(nonatomic) Uint32 eventType;
- (void)reopen:(NSAppleEventDescriptor *)event reply:(NSAppleEventDescriptor *)reply;
@end

@implementation TTPlayerReopenHandler
- (void)reopen:(NSAppleEventDescriptor *)event reply:(NSAppleEventDescriptor *)reply {
    static_cast<void>(event);
    static_cast<void>(reply);
    SDL_Event pending{};
    pending.type = self.eventType;
    SDL_PushEvent(&pending);
}
@end

namespace ttplayer::app {
    namespace {
        NSWindow *cocoaWindow(SDL_Window *window) {
            return (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
        }

        CGFloat mainScreenTop() {
            return NSMaxY(NSScreen.screens.firstObject.frame);
        }
    }

    bool macWindowBounds(SDL_Window *window, SDL_Rect &bounds) {
        NSWindow *native = cocoaWindow(window);
        if (!native) return false;
        const auto frame = native.frame;
        bounds = {static_cast<int>(std::round(frame.origin.x)),
                  static_cast<int>(std::round(mainScreenTop() - NSMaxY(frame))),
                  static_cast<int>(std::round(frame.size.width)), static_cast<int>(std::round(frame.size.height))};
        return true;
    }

    bool moveMacWindow(SDL_Window *window, const int left, const int top) {
        NSWindow *native = cocoaWindow(window);
        if (!native) return false;
        [native setFrameTopLeftPoint:NSMakePoint(left, mainScreenTop() - top)];
        return true;
    }

    void setMacWindowBounds(SDL_Window *window, const SDL_Rect bounds) {
        NSWindow *native = cocoaWindow(window);
        if (!native) return;
        const auto frame = NSMakeRect(bounds.x, mainScreenTop() - bounds.y - bounds.h, bounds.w, bounds.h);
        if (!NSEqualRects(native.frame, frame)) [native setFrame:frame display:YES];
    }

    SDL_Point macWindowMinimumSize(SDL_Window *window) {
        SDL_Point minimum{};
        SDL_GetWindowMinimumSize(window, &minimum.x, &minimum.y);
        if (NSWindow *native = cocoaWindow(window)) {
            const auto frame = native.frame;
            const auto content = [native contentRectForFrameRect:frame];
            minimum.x += static_cast<int>(std::round(frame.size.width - content.size.width));
            minimum.y += static_cast<int>(std::round(frame.size.height - content.size.height));
        }
        return minimum;
    }

    TTF_Font *openMacFont(const std::string_view family, const int size) {
        if (family.empty()) return nullptr;
        @autoreleasepool {
            NSString *name = [[NSString alloc] initWithBytes:family.data() length:family.size() encoding:NSUTF8StringEncoding];
            if (!name) return nullptr;
            NSDictionary<NSString *, NSString *> *aliases = @{
                @"simsun": @"Songti SC", @"simhei": @"Heiti SC",
                @"microsoft yahei": @"PingFang SC", @"msyh": @"PingFang SC",
                @"tahoma": @"Helvetica Neue", @"\u5B8B\u4F53": @"Songti SC",
                @"\u9ED1\u4F53": @"Heiti SC", @"\u5FAE\u8F6F\u96C5\u9ED1": @"PingFang SC"
            };
            name = aliases[name.lowercaseString] ?: name;
            NSFont *font = [NSFont fontWithName:name size:16];
            if (!font) {
                NSFontDescriptor *descriptor = [NSFontDescriptor fontDescriptorWithFontAttributes:@{NSFontFamilyAttribute: name}];
                descriptor = [descriptor matchingFontDescriptorWithMandatoryKeys:[NSSet setWithObject:NSFontFamilyAttribute]];
                if (descriptor) font = [NSFont fontWithDescriptor:descriptor size:16];
            }
            if (!font) return nullptr;
            NSURL *url = CFBridgingRelease(CTFontCopyAttribute((__bridge CTFontRef)font, kCTFontURLAttribute));
            if (!url.fileURL) return nullptr;
            auto *result = TTF_OpenFont(url.path.UTF8String, static_cast<float>(size));
            if (!result) return nullptr;
            const int faces = TTF_GetNumFontFaces(result);
            const auto *resolvedFamily = font.familyName.UTF8String;
            const auto matches = [&](TTF_Font *candidate) {
                const auto *candidateFamily = TTF_GetFontFamilyName(candidate);
                return candidateFamily && resolvedFamily && SDL_strcasecmp(candidateFamily, resolvedFamily) == 0;
            };
            if (matches(result)) return result;
            for (int face = 1; face < faces; ++face) {
                const auto properties = SDL_CreateProperties();
                SDL_SetStringProperty(properties, TTF_PROP_FONT_CREATE_FILENAME_STRING, url.path.UTF8String);
                SDL_SetFloatProperty(properties, TTF_PROP_FONT_CREATE_SIZE_FLOAT, static_cast<float>(size));
                SDL_SetNumberProperty(properties, TTF_PROP_FONT_CREATE_FACE_NUMBER, face);
                auto *candidate = TTF_OpenFontWithProperties(properties);
                SDL_DestroyProperties(properties);
                if (!candidate) continue;
                if (matches(candidate)) {
                    TTF_CloseFont(result);
                    return candidate;
                }
                TTF_CloseFont(candidate);
            }
            return result;
        }
    }

    void raiseMacPopup(SDL_Window *window) {
        @autoreleasepool {
            NSWindow *native = (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
            if (!native) return;
            native.level = NSPopUpMenuWindowLevel;
            native.hidesOnDeactivate = YES;
            native.collectionBehavior = (native.collectionBehavior & ~(NSWindowCollectionBehaviorManaged | NSWindowCollectionBehaviorFullScreenPrimary)) |
                NSWindowCollectionBehaviorTransient | NSWindowCollectionBehaviorFullScreenAuxiliary;
            [native orderFront:nil];
        }
    }

    void configureMacDesktop(SDL_Window *window, const bool locked) {
        @autoreleasepool {
            NSWindow *native = (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
            if (!native) return;
            native.ignoresMouseEvents = locked;
            native.hidesOnDeactivate = NO;
            native.collectionBehavior = (native.collectionBehavior & ~(NSWindowCollectionBehaviorMoveToActiveSpace | NSWindowCollectionBehaviorFullScreenPrimary)) |
                NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
        }
    }

    struct MacApplication::State {
        TTPlayerReopenHandler *reopen;
        NSMutableArray<id> *observers;
        std::function<void(SDL_Window *, MacWindowEvent)> windowEvent;
        bool dispatching = false;
        NSTimer *liveTimer = nil;

        void dispatch(SDL_Window *window, const MacWindowEvent event) {
            if (dispatching) return;
            dispatching = true;
            try { windowEvent(window, event); }
            catch (const std::exception &error) { SDL_Log("macOS window: %s", error.what()); }
            dispatching = false;
        }

        void track(SDL_Window *window) {
            [liveTimer invalidate];
            const auto identifier = SDL_GetWindowID(window);
            liveTimer = [NSTimer timerWithTimeInterval:0.03 repeats:YES block:^(NSTimer *timer) {
                if (!SDL_IsMainThread()) return;
                auto *target = SDL_GetWindowFromID(identifier);
                if (!target) { [timer invalidate]; return; }
                NSWindow *native = (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(target), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
                if (native.inLiveResize || (NSEvent.pressedMouseButtons & 1)) dispatch(target, MacWindowEvent::Tick);
                else {
                    dispatch(target, MacWindowEvent::EndMove);
                    [timer invalidate];
                }
            }];
            [NSRunLoop.mainRunLoop addTimer:liveTimer forMode:NSRunLoopCommonModes];
        }

        State(const Uint32 reopenEvent, std::function<void(SDL_Window *, MacWindowEvent)> callback)
            : windowEvent(std::move(callback)) {
            reopen = [TTPlayerReopenHandler new];
            reopen.eventType = reopenEvent;
            observers = [NSMutableArray new];
            NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
            [observers addObject:[center addObserverForName:NSApplicationDidBecomeActiveNotification object:nil queue:nil usingBlock:^(NSNotification *) {
                [reopen reopen:nil reply:nil];
            }]];
            const std::array<std::pair<NSNotificationName, MacWindowEvent>, 5> notifications{{
                {NSWindowWillMoveNotification, MacWindowEvent::BeginMove},
                {NSWindowDidMoveNotification, MacWindowEvent::Move},
                {NSWindowWillStartLiveResizeNotification, MacWindowEvent::BeginResize},
                {NSWindowDidResizeNotification, MacWindowEvent::Resize},
                {NSWindowDidEndLiveResizeNotification, MacWindowEvent::EndResize}
            }};
            for (const auto &notification: notifications) {
                const auto name = notification.first;
                const auto kind = notification.second;
                [observers addObject:[center addObserverForName:name object:nil queue:nil usingBlock:^(NSNotification *notification) {
                    if (dispatching || !SDL_IsMainThread()) return;
                    int count = 0;
                    auto **windows = SDL_GetWindows(&count);
                    SDL_Window *target = nullptr;
                    for (int index = 0; index < count; ++index) {
                        auto *native = SDL_GetPointerProperty(SDL_GetWindowProperties(windows[index]), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
                        if (native == (__bridge void *)notification.object) target = windows[index];
                    }
                    SDL_free(windows);
                    if (!target) return;
                    dispatch(target, kind);
                    if (kind == MacWindowEvent::BeginMove || kind == MacWindowEvent::BeginResize) track(target);
                    else if (kind == MacWindowEvent::EndResize) [liveTimer invalidate];
                }]];
            }
            [NSAppleEventManager.sharedAppleEventManager setEventHandler:reopen andSelector:@selector(reopen:reply:)
                forEventClass:kCoreEventClass andEventID:kAEReopenApplication];
        }

        ~State() {
            [liveTimer invalidate];
            for (id observer in observers) [NSNotificationCenter.defaultCenter removeObserver:observer];
            [NSAppleEventManager.sharedAppleEventManager removeEventHandlerForEventClass:kCoreEventClass andEventID:kAEReopenApplication];
        }
    };

    MacApplication::MacApplication(const Uint32 reopenEvent, std::function<void(SDL_Window *, MacWindowEvent)> windowEvent)
        : state_(std::make_unique<State>(reopenEvent, std::move(windowEvent))) {}

    MacApplication::~MacApplication() = default;
}
