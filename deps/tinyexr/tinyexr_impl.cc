#ifdef __EMSCRIPTEN__
#define TINYEXR_USE_THREAD 0
#else
#define TINYEXR_USE_THREAD 1
#endif
#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"
