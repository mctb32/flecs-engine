#ifndef FLECS_ENGINE_TRACY_HOOKS_H
#define FLECS_ENGINE_TRACY_HOOKS_H

#ifdef FLECS_ENGINE_TRACY
  #include <tracy/TracyC.h>
  #define FLECS_TRACY_ZONE_BEGIN(name) \
      TracyCZoneN(__tracy_zone_ctx, name, 1)
  #define FLECS_TRACY_ZONE_END \
      TracyCZoneEnd(__tracy_zone_ctx)
  #define FLECS_TRACY_FRAME_MARK TracyCFrameMark
#else
  #define FLECS_TRACY_ZONE_BEGIN(name) (void)0
  #define FLECS_TRACY_ZONE_END (void)0
  #define FLECS_TRACY_FRAME_MARK (void)0
#endif

#endif
