// stb single-header implementations must be compiled in exactly ONE translation
// unit by defining these macros before including the headers.
// Putting them here keeps main.cpp and the glitch headers clean.

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
