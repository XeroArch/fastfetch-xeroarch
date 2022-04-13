#ifdef FF_HAVE_IMAGEMAGICK6

#include "image.h"
#include <magick/MagickCore.h>

static FF_LIBRARY_SYMBOL(ResizeImage);

static void* logoResize(const void* image, size_t width, size_t height, void* exceptionInfo)
{
    return ffResizeImage(image, width, height, UndefinedFilter, 1.0, exceptionInfo);
}

FFLogoImageResult ffLogoPrintImageIM6(FFinstance* instance, FFLogoType type)
{
    FF_LIBRARY_LOAD(imageMagick, instance->config.libImageMagick, FF_LOGO_SIXEL_RESULT_INIT_ERROR, "libMagickCore-6.Q16HDRI.so", 8, "libMagickCore-6.Q16.so", 8)
    FF_LIBRARY_LOAD_SYMBOL_ADRESS(imageMagick, ffResizeImage, ResizeImage, FF_LOGO_SIXEL_RESULT_INIT_ERROR);
    return ffLogoPrintImageImpl(instance, imageMagick, logoResize, type);
}

#endif
