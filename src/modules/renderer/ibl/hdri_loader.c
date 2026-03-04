#include "hdri_loader.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool flecsHdriPathHasExt(
    const char *path,
    const char *ext)
{
    size_t path_len = strlen(path);
    size_t ext_len = strlen(ext);
    if (path_len < ext_len) {
        return false;
    }

    const char *suffix = path + (path_len - ext_len);
    for (size_t i = 0; i < ext_len; i ++) {
        unsigned char a = (unsigned char)suffix[i];
        unsigned char b = (unsigned char)ext[i];
        if (tolower(a) != tolower(b)) {
            return false;
        }
    }

    return true;
}

static bool flecsHdriLoadImageIo(
    const char *path,
    FlecsHdriImage *out_image)
{
    bool ok = false;
    CFStringRef cf_path = NULL;
    CFURLRef url = NULL;
    CGImageSourceRef source = NULL;
    CGImageRef image = NULL;
    CGColorSpaceRef color_space = NULL;
    CGContextRef context = NULL;
    float *pixels = NULL;
    CFDictionaryRef decode_options = NULL;
    const void *option_keys[2];
    const void *option_values[2];

    cf_path = CFStringCreateWithCString(
        kCFAllocatorDefault,
        path,
        kCFStringEncodingUTF8);
    if (!cf_path) {
        goto cleanup;
    }

    url = CFURLCreateWithFileSystemPath(
        kCFAllocatorDefault,
        cf_path,
        kCFURLPOSIXPathStyle,
        false);
    if (!url) {
        goto cleanup;
    }

    source = CGImageSourceCreateWithURL(url, NULL);
    if (!source) {
        goto cleanup;
    }

    option_keys[0] = kCGImageSourceShouldAllowFloat;
    option_values[0] = kCFBooleanTrue;
    option_keys[1] = kCGImageSourceShouldCache;
    option_values[1] = kCFBooleanFalse;
    decode_options = CFDictionaryCreate(
        kCFAllocatorDefault,
        option_keys,
        option_values,
        2,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    if (!decode_options) {
        goto cleanup;
    }

    image = CGImageSourceCreateImageAtIndex(source, 0, decode_options);
    if (!image) {
        goto cleanup;
    }

    size_t width = CGImageGetWidth(image);
    size_t height = CGImageGetHeight(image);
    if (!width || !height || width > INT32_MAX || height > INT32_MAX) {
        goto cleanup;
    }

    size_t row_bytes = width * 4u * sizeof(float);
    pixels = malloc(row_bytes * height);
    if (!pixels) {
        goto cleanup;
    }

    color_space = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB);
    if (!color_space) {
        color_space = CGColorSpaceCreateDeviceRGB();
    }
    if (!color_space) {
        goto cleanup;
    }

    CGBitmapInfo bitmap_info =
        kCGImageAlphaNoneSkipLast |
        kCGBitmapFloatComponents |
        kCGBitmapByteOrder32Little;

    context = CGBitmapContextCreate(
        pixels,
        width,
        height,
        32,
        row_bytes,
        color_space,
        bitmap_info);
    if (!context) {
        goto cleanup;
    }

    CGContextSetBlendMode(context, kCGBlendModeCopy);
    CGContextDrawImage(
        context,
        CGRectMake(0.0f, 0.0f, (CGFloat)width, (CGFloat)height),
        image);

    out_image->width = (int32_t)width;
    out_image->height = (int32_t)height;
    out_image->pixels_rgba32f = pixels;
    pixels = NULL;
    ok = true;

cleanup:
    if (context) {
        CGContextRelease(context);
    }
    if (color_space) {
        CGColorSpaceRelease(color_space);
    }
    if (image) {
        CGImageRelease(image);
    }
    if (source) {
        CFRelease(source);
    }
    if (decode_options) {
        CFRelease(decode_options);
    }
    if (url) {
        CFRelease(url);
    }
    if (cf_path) {
        CFRelease(cf_path);
    }
    free(pixels);

    return ok;
}

bool flecsHdriLoad(
    const char *path,
    FlecsHdriImage *out_image)
{
    if (!path || !out_image) {
        return false;
    }

    out_image->width = 0;
    out_image->height = 0;
    out_image->pixels_rgba32f = NULL;

    if (!flecsHdriPathHasExt(path, ".exr") &&
        !flecsHdriPathHasExt(path, ".hdr"))
    {
        return false;
    }

    return flecsHdriLoadImageIo(path, out_image);
}

void flecsHdriImageFree(
    FlecsHdriImage *image)
{
    if (!image) {
        return;
    }

    free(image->pixels_rgba32f);
    image->pixels_rgba32f = NULL;
    image->width = 0;
    image->height = 0;
}
