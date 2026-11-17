#include "app/PlatformMac.hpp"

#import <AppKit/AppKit.h>

namespace ttplayer::app {
    SDL_Surface *loadMacIcon(const std::string_view content, const int size) {
        if (content.empty() || size < 1 || size > 1024) return nullptr;
        @autoreleasepool {
            NSData *data = [NSData dataWithBytes:content.data() length:content.size()];
            NSImage *image = [[NSImage alloc] initWithData:data];
            if (!image) return nullptr;
            NSBitmapImageRep *bitmap = [[NSBitmapImageRep alloc]
                initWithBitmapDataPlanes:nullptr pixelsWide:size pixelsHigh:size
                bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
                colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:size * 4 bitsPerPixel:32];
            if (!bitmap) return nullptr;
            [NSGraphicsContext saveGraphicsState];
            NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
            NSGraphicsContext.currentContext.imageInterpolation = NSImageInterpolationHigh;
            [image drawInRect:NSMakeRect(0, 0, size, size) fromRect:NSZeroRect
                operation:NSCompositingOperationCopy fraction:1.0 respectFlipped:YES hints:nil];
            [NSGraphicsContext restoreGraphicsState];
            auto *surface = SDL_CreateSurface(size, size, SDL_PIXELFORMAT_RGBA32);
            if (!surface) return nullptr;
            for (int row = 0; row < size; ++row) for (int column = 0; column < size; ++column) {
                NSColor *color = [[bitmap colorAtX:column y:row] colorUsingColorSpace:NSColorSpace.deviceRGBColorSpace];
                auto *pixel = static_cast<Uint8 *>(surface->pixels) + row * surface->pitch + column * 4;
                pixel[0] = static_cast<Uint8>(color.redComponent * 255);
                pixel[1] = static_cast<Uint8>(color.greenComponent * 255);
                pixel[2] = static_cast<Uint8>(color.blueComponent * 255);
                pixel[3] = static_cast<Uint8>(color.alphaComponent * 255);
            }
            return surface;
        }
    }
}
