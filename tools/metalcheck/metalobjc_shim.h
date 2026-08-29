/* A stand-in for <Metal/Metal.h> and <QuartzCore/CAMetalLayer.h>, declaring
   only what display.c's Metal readback touches, so it can be type-checked by
   clang on a machine with no macOS SDK. Same idea as metalshim.h, one layer
   up: that one checks the shader, this one checks the Objective-C.

   It cannot verify that the selectors match Apple's real ones -- only that
   the code is internally consistent and well typed. A wrong selector spelled
   consistently in both places would pass. Everything else (arity, argument
   and return types, the bridge casts, property access) it does catch. */
#ifndef HOLO_METALOBJC_SHIM_H
#define HOLO_METALOBJC_SHIM_H

#include <stddef.h>
#include <stdint.h>

/* Foundation supplies these in a real build; Metal.h drags it in. */
#ifndef nil
#define nil ((id)0)
#endif

typedef unsigned long NSUInteger;
typedef signed char BOOL;
#define NO ((BOOL)0)
#define YES ((BOOL)1)

typedef NSUInteger MTLPixelFormat;
enum { MTLPixelFormatBGRA8Unorm = 80, MTLPixelFormatBGRA8Unorm_sRGB = 81 };

typedef NSUInteger MTLStorageMode;
enum { MTLStorageModeShared = 0, MTLStorageModeManaged = 1,
       MTLStorageModePrivate = 2 };

typedef NSUInteger MTLTextureUsage;
enum { MTLTextureUsageShaderRead = 1, MTLTextureUsageRenderTarget = 4 };

typedef NSUInteger MTLCommandBufferStatus;
enum { MTLCommandBufferStatusNotEnqueued = 0, MTLCommandBufferStatusCompleted = 4 };

typedef struct { NSUInteger x, y, z; } MTLOrigin;
typedef struct { NSUInteger width, height, depth; } MTLSize;
typedef struct { MTLOrigin origin; MTLSize size; } MTLRegion;

static inline MTLOrigin MTLOriginMake(NSUInteger x, NSUInteger y, NSUInteger z) {
    MTLOrigin o = { x, y, z };
    return o;
}
static inline MTLSize MTLSizeMake(NSUInteger w, NSUInteger h, NSUInteger d) {
    MTLSize s = { w, h, d };
    return s;
}
static inline MTLRegion MTLRegionMake2D(NSUInteger x, NSUInteger y,
                                        NSUInteger w, NSUInteger h) {
    MTLRegion r = { { x, y, 0 }, { w, h, 1 } };
    return r;
}

@protocol MTLTexture;
@protocol MTLDevice;

@protocol NSObject
@end

@interface NSObject <NSObject>
@end

@interface MTLTextureDescriptor : NSObject
+ (MTLTextureDescriptor *)texture2DDescriptorWithPixelFormat:(MTLPixelFormat)fmt
                                                       width:(NSUInteger)w
                                                      height:(NSUInteger)h
                                                   mipmapped:(BOOL)mip;
@property MTLStorageMode storageMode;
@property MTLTextureUsage usage;
@end

@protocol MTLTexture <NSObject>
@property (readonly) NSUInteger width;
@property (readonly) NSUInteger height;
@property (readonly) MTLPixelFormat pixelFormat;
- (void)getBytes:(void *)bytes
     bytesPerRow:(NSUInteger)row
      fromRegion:(MTLRegion)region
     mipmapLevel:(NSUInteger)level;
@end

@protocol MTLBlitCommandEncoder <NSObject>
- (void)copyFromTexture:(id<MTLTexture>)src
            sourceSlice:(NSUInteger)ss
            sourceLevel:(NSUInteger)sl
           sourceOrigin:(MTLOrigin)so
             sourceSize:(MTLSize)sz
              toTexture:(id<MTLTexture>)dst
       destinationSlice:(NSUInteger)ds
       destinationLevel:(NSUInteger)dl
      destinationOrigin:(MTLOrigin)dorg;
- (void)endEncoding;
@end

@protocol MTLCommandBuffer <NSObject>
@property (readonly) MTLCommandBufferStatus status;
- (id<MTLBlitCommandEncoder>)blitCommandEncoder;
- (void)commit;
- (void)waitUntilCompleted;
@end

@protocol MTLCommandQueue <NSObject>
- (id<MTLCommandBuffer>)commandBuffer;
@end

@protocol MTLDevice <NSObject>
- (id<MTLCommandQueue>)newCommandQueue;
- (id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor *)desc;
@end

@protocol CAMetalDrawable <NSObject>
@property (readonly) id<MTLTexture> texture;
@end

/* The sokol entry points the readback reaches for, with sokol's own
   signatures -- both return const void*, which is why the bridge casts
   have to be written carefully. */
const void *sg_mtl_device(void);
const void *sapp_metal_get_current_drawable(void);
int sapp_width(void);
int sapp_height(void);

#endif
