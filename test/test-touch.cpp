/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2018 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <linux/input.h>
#include <xkbcommon/xkbcommon.h>

#include "test-touch.h"
#include "test-egl.h"

#include "westeros-compositor.h"
#include "wayland-client.h"
#include "wayland-egl.h"

#include "simpleshell-client-protocol.h"

namespace Touch
{

typedef struct _TestCtx
{
   TestEGLCtx eglCtx;
   struct wl_display *display;
   struct wl_compositor *compositor;
   struct wl_seat *seat;
   struct wl_simple_shell *shell;
   struct wl_pointer *pointer;
   struct wl_keyboard *keyboard;
   struct wl_touch *touch;
   int windowWidth;
   int windowHeight;
   struct xkb_context *xkbCtx;
   struct xkb_keymap *xkbKeymap;
   struct xkb_state *xkbState;
   xkb_mod_index_t modAlt;
   xkb_mod_index_t modCtrl;
   xkb_mod_index_t modShift;
   xkb_mod_index_t modCaps;
   unsigned int modMask;

   struct wl_surface *surface1;
   uint32_t surfaceId1;
   char surfaceName1[33];
   struct wl_egl_window *wlEglWindow1;
   EGLSurface eglSurfaceWindow1;

   struct wl_surface *surface2;
   uint32_t surfaceId2;
   char surfaceName2[33];
   struct wl_egl_window *wlEglWindow2;
   EGLSurface eglSurfaceWindow2;

   bool keyboardMap;
   bool keyboardEnter;
   bool keyboardLeave;
   bool keyboardRepeatInfo;
   bool keyAlt;
   bool keyCtrl;
   bool keyShift;
   bool keyCaps;
   int keyPressed;
   struct wl_surface *surfaceWithKeyInput;

   bool touchFrameEnd;
   int touchCount;
   WstTouchSet touchSet;
} TestCtx;

static void touchHandleDown( void *data, struct wl_touch *touch,
                             uint32_t serial, uint32_t time, struct wl_surface *surface,
                             int32_t id, wl_fixed_t sx, wl_fixed_t sy )
{
   TestCtx *ctx= (TestCtx*)data;
   int x, y;
   int i;

   x= wl_fixed_to_int( sx );
   y= wl_fixed_to_int( sy );

   for( i= 0; i < WST_MAX_TOUCH; ++i )
   {
      if ( !ctx->touchSet.touch[i].valid )
      {
         ctx->touchSet.touch[i].valid= true;
         ctx->touchSet.touch[i].id= id;
         ctx->touchSet.touch[i].x= x;
         ctx->touchSet.touch[i].y= y;
         ++ctx->touchCount;
         break;
      }
   }
}

static void touchHandleUp( void *data, struct wl_touch *touch,
                           uint32_t serial, uint32_t time, int32_t id )
{
   TestCtx *ctx= (TestCtx*)data;
   int i;

   for( i= 0; i < WST_MAX_TOUCH; ++i )
   {   
      if ( ctx->touchSet.touch[i].valid && (ctx->touchSet.touch[i].id == id) )
      {
         ctx->touchSet.touch[i].id= 0;
         --ctx->touchCount;
         break;
      }
   }
}

static void touchHandleMotion( void *data, struct wl_touch *touch,
                               uint32_t time, int32_t id, wl_fixed_t sx, wl_fixed_t sy )
{
   TestCtx *ctx= (TestCtx*)data;
   int x, y;
   int i;

   x= wl_fixed_to_int( sx );
   y= wl_fixed_to_int( sy );

   for( i= 0; i < WST_MAX_TOUCH; ++i )
   {   
      if ( ctx->touchSet.touch[i].valid && (ctx->touchSet.touch[i].id == id) )
      {
         ctx->touchSet.touch[i].x= x;
         ctx->touchSet.touch[i].y= y;
         break;
      }
   }
}

static void touchHandleFrame( void *data, struct wl_touch *touch )
{
   TestCtx *ctx= (TestCtx*)data;
   int i;

   ctx->touchFrameEnd= true;

   for( i= 0; i < WST_MAX_TOUCH; ++i )
   {
      if ( ctx->touchSet.touch[i].id == 0 )
      {
         ctx->touchSet.touch[i].valid= false;
      }
   }
}

static const struct wl_touch_listener touchListener= {
   touchHandleDown,
   touchHandleUp,
   touchHandleMotion,
   touchHandleFrame
};

static void pointerEnter( void* data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy )
{
}

static void pointerLeave( void* data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface )
{
}

static void pointerMotion( void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t sx, wl_fixed_t sy )
{
}

static void pointerButton( void *data, struct wl_pointer *pointer, uint32_t serial,
                           uint32_t time, uint32_t button, uint32_t state )
{
}

static void pointerAxis( void *data, struct wl_pointer *pointer, uint32_t time,
                         uint32_t axis, wl_fixed_t value )
{
}

static const struct wl_pointer_listener pointerListener = {
   pointerEnter,
   pointerLeave,
   pointerMotion,
   pointerButton,
   pointerAxis
};

static void keyboardKeymap( void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size )
{
   TestCtx *ctx= (TestCtx*)data;

   if ( format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 )
   {
      void *map= mmap( 0, size, PROT_READ, MAP_SHARED, fd, 0 );
      if ( map != MAP_FAILED )
      {
         if ( !ctx->xkbCtx )
         {
            ctx->xkbCtx= xkb_context_new( XKB_CONTEXT_NO_FLAGS );
         }
         else
         {
            printf("error: xkb_context_new failed\n");
         }
         if ( ctx->xkbCtx )
         {
            if ( ctx->xkbKeymap )
            {
               xkb_keymap_unref( ctx->xkbKeymap );
               ctx->xkbKeymap= 0;
            }
            ctx->xkbKeymap= xkb_keymap_new_from_string( ctx->xkbCtx, (char*)map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
            if ( !ctx->xkbKeymap )
            {
               printf("error: xkb_keymap_new_from_string failed\n");
            }
            if ( ctx->xkbState )
            {
               xkb_state_unref( ctx->xkbState );
               ctx->xkbState= 0;
            }
            ctx->xkbState= xkb_state_new( ctx->xkbKeymap );
            if ( !ctx->xkbState )
            {
               printf("error: xkb_state_new failed\n");
            }
            if ( ctx->xkbKeymap )
            {
               ctx->modAlt= xkb_keymap_mod_get_index( ctx->xkbKeymap, XKB_MOD_NAME_ALT );
               ctx->modCtrl= xkb_keymap_mod_get_index( ctx->xkbKeymap, XKB_MOD_NAME_CTRL );
               ctx->modShift= xkb_keymap_mod_get_index( ctx->xkbKeymap, XKB_MOD_NAME_SHIFT );
               ctx->modCaps= xkb_keymap_mod_get_index( ctx->xkbKeymap, XKB_MOD_NAME_CAPS );
            }
            munmap( map, size );

            ctx->keyboardMap= true;
         }
      }
   }

   close( fd );
}
static void keyboardEnter( void *data, struct wl_keyboard *keyboard, uint32_t serial,
                           struct wl_surface *surface, struct wl_array *keys )
{
   TestCtx *ctx= (TestCtx*)data;
   printf("keyboard enter\n");
   ctx->keyboardEnter= true;
   ctx->surfaceWithKeyInput= surface;
}

static void keyboardLeave( void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface )
{
   TestCtx *ctx= (TestCtx*)data;
   printf("keyboard leave\n");
   ctx->keyboardLeave= true;
   ctx->surfaceWithKeyInput= 0;
}

static void keyboardKey( void *data, struct wl_keyboard *keyboard, uint32_t serial,
                         uint32_t time, uint32_t key, uint32_t state )
{
   TestCtx *ctx= (TestCtx*)data;
   if ( state == WL_KEYBOARD_KEY_STATE_PRESSED )
   {
      ctx->keyPressed= key;
   }
   else if ( state == WL_KEYBOARD_KEY_STATE_RELEASED )
   {
      ctx->keyPressed= 0;
   }
}

static void keyboardModifiers( void *data, struct wl_keyboard *keyboard, uint32_t serial,
                               uint32_t mods_depressed, uint32_t mods_latched,
                               uint32_t mods_locked, uint32_t group )
{
   TestCtx *ctx= (TestCtx*)data;
   int wasActive, nowActive;
   if ( ctx->xkbState )
   {
      xkb_state_update_mask( ctx->xkbState, mods_depressed, mods_latched, mods_locked, 0, 0, group );

      wasActive= (ctx->modMask & (1<<ctx->modAlt));
      nowActive= (mods_depressed & (1<<ctx->modAlt));
      if ( nowActive != wasActive )
      {
         ctx->modMask ^= (1<<ctx->modAlt);
         ctx->keyAlt= nowActive;
      }

      wasActive= (ctx->modMask & (1<<ctx->modCtrl));
      nowActive= (mods_depressed & (1<<ctx->modCtrl));
      if ( nowActive != wasActive )
      {
         ctx->modMask ^= (1<<ctx->modCtrl);
         ctx->keyCtrl= nowActive;
      }

      wasActive= (ctx->modMask & (1<<ctx->modShift));
      nowActive= (mods_depressed & (1<<ctx->modShift));
      if ( nowActive != wasActive )
      {
         ctx->modMask ^= (1<<ctx->modShift);
         ctx->keyShift= nowActive;
      }

      wasActive= (ctx->modMask & (1<<ctx->modCaps));
      nowActive= (mods_locked & (1<<ctx->modCaps));
      if ( nowActive != wasActive )
      {
         ctx->modMask ^= (1<<ctx->modCaps);
         ctx->keyCaps= nowActive;
      }
   }
}

static void keyboardRepeatInfo( void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay )
{
   TestCtx *ctx= (TestCtx*)data;
   ctx->keyboardRepeatInfo= true;
}

static const struct wl_keyboard_listener keyboardListener= {
   keyboardKeymap,
   keyboardEnter,
   keyboardLeave,
   keyboardKey,
   keyboardModifiers,
   keyboardRepeatInfo
};

static void seatCapabilities( void *data, struct wl_seat *seat, uint32_t capabilities )
{
   TestCtx *ctx= (TestCtx*)data;
   if ( capabilities & WL_SEAT_CAPABILITY_KEYBOARD )
   {
      ctx->keyboard= wl_seat_get_keyboard( ctx->seat );
      printf("keyboard %p\n", ctx->keyboard );
      wl_keyboard_add_listener( ctx->keyboard, &keyboardListener, ctx );
      wl_display_roundtrip(ctx->display);
   }
   if ( capabilities & WL_SEAT_CAPABILITY_POINTER )
   {
      ctx->pointer= wl_seat_get_pointer( ctx->seat );
      printf("pointer %p\n", ctx->pointer );
      wl_pointer_add_listener( ctx->pointer, &pointerListener, ctx );
      wl_display_roundtrip(ctx->display);
   }
   if ( capabilities & WL_SEAT_CAPABILITY_TOUCH )
   {
      ctx->touch= wl_seat_get_touch( ctx->seat );
      printf("touch %p\n", ctx->touch );
      wl_touch_add_listener( ctx->touch, &touchListener, ctx );
   }   
}

static void seatName( void *data, struct wl_seat *seat, const char *name )
{
}

static const struct wl_seat_listener seatListener = {
   seatCapabilities,
   seatName
};

static void shellSurfaceId(void *data,
                           struct wl_simple_shell *wl_simple_shell,
                           struct wl_surface *surface,
                           uint32_t surfaceId)
{
   TestCtx *ctx= (TestCtx*)data;
   if ( surface == ctx->surface1 )
   {
      ctx->surfaceId1= surfaceId;
      sprintf( ctx->surfaceName1, "test-surface-%x", surfaceId );
      wl_simple_shell_set_name( ctx->shell, surfaceId, ctx->surfaceName1 );
   }
   else if ( surface == ctx->surface2 )
   {
      ctx->surfaceId2= surfaceId;
      sprintf( ctx->surfaceName2, "test-surface-%x", surfaceId );
      wl_simple_shell_set_name( ctx->shell, surfaceId, ctx->surfaceName2 );
   }
}

static void shellSurfaceCreated(void *data,
                                struct wl_simple_shell *wl_simple_shell,
                                uint32_t surfaceId,
                                const char *name)
{
}

static void shellSurfaceDestroyed(void *data,
                                  struct wl_simple_shell *wl_simple_shell,
                                  uint32_t surfaceId,
                                  const char *name)
{
}

static void shellSurfaceStatus(void *data,
                               struct wl_simple_shell *wl_simple_shell,
                               uint32_t surfaceId,
                               const char *name,
                               uint32_t visible,
                               int32_t x,
                               int32_t y,
                               int32_t width,
                               int32_t height,
                               wl_fixed_t opacity,
                               wl_fixed_t zorder)
{
}

static void shellGetSurfacesDone(void *data,
                                 struct wl_simple_shell *wl_simple_shell)
{
}

static const struct wl_simple_shell_listener shellListener = 
{
   shellSurfaceId,
   shellSurfaceCreated,
   shellSurfaceDestroyed,
   shellSurfaceStatus,
   shellGetSurfacesDone
};

static void registryHandleGlobal(void *data, 
                                 struct wl_registry *registry, uint32_t id,
                                 const char *interface, uint32_t version)
{
   TestCtx *ctx= (TestCtx*)data;
   int len;

   len= strlen(interface);

   if ( (len==13) && !strncmp(interface, "wl_compositor", len) ) {
      ctx->compositor= (struct wl_compositor*)wl_registry_bind(registry, id, &wl_compositor_interface, 1);
      printf("compositor %p\n", ctx->compositor);
   }
   else if ( (len==7) && !strncmp(interface, "wl_seat", len) ) {
      ctx->seat= (struct wl_seat*)wl_registry_bind(registry, id, &wl_seat_interface, 4);
      printf("seat %p\n", ctx->seat);
      wl_seat_add_listener(ctx->seat, &seatListener, ctx);
      wl_display_roundtrip(ctx->display);
   }
   else if ( (len==15) && !strncmp(interface, "wl_simple_shell", len) ) {
      ctx->shell= (struct wl_simple_shell*)wl_registry_bind(registry, id, &wl_simple_shell_interface, 1);      
      printf("shell %p\n", ctx->shell );
      wl_simple_shell_add_listener(ctx->shell, &shellListener, ctx);
   }
}

static void registryHandleGlobalRemove(void *data, 
                                      struct wl_registry *registry,
                                      uint32_t name)
{
}

static const struct wl_registry_listener registryListener = 
{
   registryHandleGlobal,
   registryHandleGlobalRemove
};

} // namespace Touch

#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 480

bool testCaseTouchBasicTouchInput( EMCTX *emctx )
{
   using namespace Touch;

   bool testResult= false;
   bool result;
   const char *displayName= "test0";
   WstCompositor *wctx= 0;
   struct wl_display *display= 0;
   struct wl_registry *registry= 0;
   TestCtx testCtx;
   TestCtx *ctx= &testCtx;
   EGLBoolean b;
   int touchId= 0;
   WstTouchSet touchSet;

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   memset( &testCtx, 0, sizeof(TestCtx) );

   display= wl_display_connect(displayName);
   if ( !display )
   {
      EMERROR( "wl_display_connect failed" );
      goto exit;
   }
   ctx->display= display;

   registry= wl_display_get_registry(display);
   if ( !registry )
   {
      EMERROR( "wl_display_get_registrty failed" );
      goto exit;
   }

   wl_registry_add_listener(registry, &registryListener, ctx);

   wl_display_roundtrip(display);

   if ( !ctx->compositor || !ctx->seat || !ctx->keyboard || !ctx->pointer || !ctx->touch )
   {
      EMERROR("Failed to acquire needed compositor items");
      goto exit;
   }

   result= testSetupEGL( &ctx->eglCtx, display );
   if ( !result )
   {
      EMERROR("testSetupEGL failed");
      goto exit;
   }

   ctx->windowWidth= WINDOW_WIDTH;
   ctx->windowHeight= WINDOW_HEIGHT;

   eglSwapInterval( ctx->eglCtx.eglDisplay, 1 );

   ctx->surface1= wl_compositor_create_surface(ctx->compositor);
   printf("surface1=%p\n", ctx->surface1);   
   if ( !ctx->surface1 )
   {
      EMERROR("error: unable to create wayland surface");
      goto exit;
   }

   ctx->wlEglWindow1= wl_egl_window_create(ctx->surface1, ctx->windowWidth, ctx->windowHeight);
   if ( !ctx->wlEglWindow1 )
   {
      EMERROR("error: unable to create wl_egl_window");
      goto exit;
   }
   printf("wl_egl_window1 %p\n", ctx->wlEglWindow1);

   ctx->eglSurfaceWindow1= eglCreateWindowSurface( ctx->eglCtx.eglDisplay,
                                                   ctx->eglCtx.eglConfig,
                                                   (EGLNativeWindowType)ctx->wlEglWindow1,
                                                   NULL );
   printf("eglCreateWindowSurface: eglSurfaceWindow1 %p\n", ctx->eglSurfaceWindow1 );

   b= eglMakeCurrent( ctx->eglCtx.eglDisplay, ctx->eglSurfaceWindow1, ctx->eglSurfaceWindow1, ctx->eglCtx.eglContext );
   if ( !b )
   {
      EMERROR("error: eglMakeCurrent failed: %X", eglGetError() );
      goto exit;
   }

   eglSwapBuffers(ctx->eglCtx.eglDisplay, ctx->eglSurfaceWindow1);

   wl_display_roundtrip(display);

   usleep( 33000 );

   if ( ctx->surfaceId1 == 0 )
   {
      EMERROR("Did not get surface id for surface 1");
      goto exit;
   }

   memset( &touchSet, 0, sizeof(touchSet) );

   // first touch
   touchSet.touch[0].id= ++touchId;
   touchSet.touch[0].x= 310;
   touchSet.touch[0].y= 110;
   touchSet.touch[0].valid= true;
   touchSet.touch[0].starting= true;
   touchSet.touch[0].stopping= false;
   touchSet.touch[0].moved= false;
   WstCompositorTouchEvent( wctx, &touchSet );

   wl_display_roundtrip(display);

   usleep( 17000 );

   wl_display_roundtrip(display);

   if ( !ctx->touchFrameEnd )
   {
      EMERROR("Missing touch frame");
      goto exit;
   }

   if ( ctx->touchCount != 1 )
   {
      EMERROR("Bad active touch count: expected (%d) actual (%d)", 1, ctx->touchCount );
      goto exit;
   }

   if (
        (ctx->touchSet.touch[0].valid == false) ||
        (ctx->touchSet.touch[0].x != 310) ||
        (ctx->touchSet.touch[0].y != 110)
      )
   {
      EMERROR("Bad touch 1: expected: valid (1 / %d) point (310,110 / %d,%d)",
        ctx->touchSet.touch[0].valid,
        ctx->touchSet.touch[0].x,
        ctx->touchSet.touch[0].y );
      goto exit;
   }

   // first touch moves and second touch starts
   touchSet.touch[0].x= 320;
   touchSet.touch[0].y= 120;
   touchSet.touch[0].valid= true;
   touchSet.touch[0].starting= false;
   touchSet.touch[0].stopping= false;
   touchSet.touch[0].moved= true;

   touchSet.touch[1].id= ++touchId;
   touchSet.touch[1].x= 210;
   touchSet.touch[1].y= 180;
   touchSet.touch[1].valid= true;
   touchSet.touch[1].starting= true;
   touchSet.touch[1].stopping= false;
   touchSet.touch[1].moved= false;
   WstCompositorTouchEvent( wctx, &touchSet );

   wl_display_roundtrip(display);

   usleep( 17000 );

   wl_display_roundtrip(display);

   if ( !ctx->touchFrameEnd )
   {
      EMERROR("Missing touch frame");
      goto exit;
   }

   if ( ctx->touchCount != 2 )
   {
      EMERROR("Bad active touch count: expected (%d) actual (%d)", 2, ctx->touchCount );
      goto exit;
   }

   if (
        (ctx->touchSet.touch[0].valid == false) ||
        (ctx->touchSet.touch[0].x != 320) ||
        (ctx->touchSet.touch[0].y != 120)
      )
   {
      EMERROR("Bad touch 1: expected: valid (1 / %d) point (320,120 / %d,%d)",
        ctx->touchSet.touch[0].valid,
        ctx->touchSet.touch[0].x,
        ctx->touchSet.touch[0].y );
      goto exit;
   }

   if (
        (ctx->touchSet.touch[1].valid == false) ||
        (ctx->touchSet.touch[1].x != 210) ||
        (ctx->touchSet.touch[1].y != 180)
      )
   {
      EMERROR("Bad touch 2: expected: valid (1 / %d) point (210,180 / %d,%d)",
        ctx->touchSet.touch[1].valid,
        ctx->touchSet.touch[1].x,
        ctx->touchSet.touch[1].y );
      goto exit;
   }

   // First touch ends, second touch moves, third touch starts
   touchSet.touch[0].starting= false;
   touchSet.touch[0].stopping= true;

   touchSet.touch[1].x= 211;
   touchSet.touch[1].y= 180;
   touchSet.touch[1].valid= true;
   touchSet.touch[1].starting= false;
   touchSet.touch[1].stopping= false;
   touchSet.touch[1].moved= true;

   touchSet.touch[2].id= ++touchId;
   touchSet.touch[2].x= 400;
   touchSet.touch[2].y= 300;
   touchSet.touch[2].valid= true;
   touchSet.touch[2].starting= true;
   touchSet.touch[2].stopping= false;
   touchSet.touch[2].moved= false;

   WstCompositorTouchEvent( wctx, &touchSet );

   wl_display_roundtrip(display);

   usleep( 17000 );

   wl_display_roundtrip(display);

   if ( ctx->touchCount != 2 )
   {
      EMERROR("Bad active touch count: expected (%d) actual (%d)", 2, ctx->touchCount );
      goto exit;
   }

   if ( ctx->touchSet.touch[0].valid )
   {
      EMERROR("Bad touch1: expected: valid (0 / %d)", ctx->touchSet.touch[0].valid );
      goto exit;
   }

   if (
        (ctx->touchSet.touch[1].valid == false) ||
        (ctx->touchSet.touch[1].x != 211) ||
        (ctx->touchSet.touch[1].y != 180)
      )
   {
      EMERROR("Bad touch 2: expected: valid (1 / %d) point (211,180 / %d,%d)",
        ctx->touchSet.touch[1].valid,
        ctx->touchSet.touch[1].x,
        ctx->touchSet.touch[1].y );
      goto exit;
   }

   if (
        (ctx->touchSet.touch[2].valid == false) ||
        (ctx->touchSet.touch[2].x != 400) ||
        (ctx->touchSet.touch[2].y != 300)
      )
   {
      EMERROR("Bad touch 3: expected: valid (1 / %d) point (400,300 / %d,%d)",
        ctx->touchSet.touch[2].valid,
        ctx->touchSet.touch[2].x,
        ctx->touchSet.touch[2].y );
      goto exit;
   }

   // Second and third touch end
   touchSet.touch[1].starting= false;
   touchSet.touch[1].stopping= true;

   touchSet.touch[2].starting= false;
   touchSet.touch[2].stopping= true;

   WstCompositorTouchEvent( wctx, &touchSet );

   wl_display_roundtrip(display);

   usleep( 17000 );

   wl_display_roundtrip(display);

   if ( ctx->touchCount != 0 )
   {
      EMERROR("Bad active touch count: expected (%d) actual (%d)", 0, ctx->touchCount );
      goto exit;
   }

   if ( EMGetWaylandThreadingIssue( emctx ) )
   {
      EMERROR( "Wayland threading issue: compositor calling wl_resource_post_event_array from multiple threads") ;
      goto exit;
   }

   testResult= true;

exit:

   if ( ctx->eglSurfaceWindow1 )
   {
      eglDestroySurface( ctx->eglCtx.eglDisplay, ctx->eglSurfaceWindow1 );
      ctx->eglSurfaceWindow1= EGL_NO_SURFACE;
   }

   if ( ctx->wlEglWindow1 )
   {
      wl_egl_window_destroy( ctx->wlEglWindow1 );
      ctx->wlEglWindow1= 0;
   }

   if ( ctx->surface1 )
   {
      wl_surface_destroy( ctx->surface1 );
      ctx->surface1= 0;
   }

   testTermEGL( &ctx->eglCtx );

   if ( ctx->keyboard )
   {
      wl_keyboard_destroy(ctx->keyboard);
      ctx->keyboard= 0;
   }

   if ( ctx->pointer )
   {
      wl_pointer_destroy(ctx->pointer);
      ctx->pointer= 0;
   }

   if ( ctx->seat )
   {
      wl_seat_destroy(ctx->seat);
      ctx->seat= 0;
   }

   if ( ctx->shell )
   {
      wl_simple_shell_destroy(ctx->shell);
      ctx->shell= 0;
   }

   if ( ctx->compositor )
   {
      wl_compositor_destroy( ctx->compositor );
      ctx->compositor= 0;
   }

   if ( registry )
   {
      wl_registry_destroy(registry);
      registry= 0;
   }

   if ( display )
   {
      wl_display_disconnect(display);
      display= 0;
   }

   WstCompositorDestroy( wctx );

   return testResult;
}

bool testCaseTouchBasicTouchInputRepeater( EMCTX *emctx )
{
   using namespace Touch;

   bool testResult= false;
   bool result;
   const char *displayName= "test0";
   const char *displayNameNested= "nested0";
   WstCompositor *wctx= 0;
   WstCompositor *wctxRepeater= 0;
   struct wl_display *display= 0;
   struct wl_registry *registry= 0;
   TestCtx testCtx;
   TestCtx *ctx= &testCtx;
   EGLBoolean b;
   int touchId= 0;
   WstTouchSet touchSet;

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }


   wctxRepeater= WstCompositorCreate();
   if ( !wctxRepeater )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctxRepeater, displayNameNested );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetIsRepeater( wctxRepeater, true );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetIsNested failed" );
      goto exit;
   }

   result= WstCompositorSetNestedDisplayName( wctxRepeater, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorStart( wctxRepeater );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }


   memset( &testCtx, 0, sizeof(TestCtx) );

   display= wl_display_connect(displayNameNested);
   if ( !display )
   {
      EMERROR( "wl_display_connect failed" );
      goto exit;
   }
   ctx->display= display;

   registry= wl_display_get_registry(display);
   if ( !registry )
   {
      EMERROR( "wl_display_get_registrty failed" );
      goto exit;
   }

   wl_registry_add_listener(registry, &registryListener, ctx);

   wl_display_roundtrip(display);

   if ( !ctx->compositor || !ctx->seat || !ctx->keyboard || !ctx->pointer || !ctx->touch )
   {
      EMERROR("Failed to acquire needed compositor items");
      goto exit;
   }

   result= testSetupEGL( &ctx->eglCtx, display );
   if ( !result )
   {
      EMERROR("testSetupEGL failed");
      goto exit;
   }

   ctx->windowWidth= WINDOW_WIDTH;
   ctx->windowHeight= WINDOW_HEIGHT;

   eglSwapInterval( ctx->eglCtx.eglDisplay, 1 );

   ctx->surface1= wl_compositor_create_surface(ctx->compositor);
   printf("surface1=%p\n", ctx->surface1);   
   if ( !ctx->surface1 )
   {
      EMERROR("error: unable to create wayland surface");
      goto exit;
   }

   ctx->wlEglWindow1= wl_egl_window_create(ctx->surface1, ctx->windowWidth, ctx->windowHeight);
   if ( !ctx->wlEglWindow1 )
   {
      EMERROR("error: unable to create wl_egl_window");
      goto exit;
   }
   printf("wl_egl_window1 %p\n", ctx->wlEglWindow1);

   ctx->eglSurfaceWindow1= eglCreateWindowSurface( ctx->eglCtx.eglDisplay,
                                                   ctx->eglCtx.eglConfig,
                                                   (EGLNativeWindowType)ctx->wlEglWindow1,
                                                   NULL );
   printf("eglCreateWindowSurface: eglSurfaceWindow1 %p\n", ctx->eglSurfaceWindow1 );

   b= eglMakeCurrent( ctx->eglCtx.eglDisplay, ctx->eglSurfaceWindow1, ctx->eglSurfaceWindow1, ctx->eglCtx.eglContext );
   if ( !b )
   {
      EMERROR("error: eglMakeCurrent failed: %X", eglGetError() );
      goto exit;
   }

   eglSwapBuffers(ctx->eglCtx.eglDisplay, ctx->eglSurfaceWindow1);

   wl_display_roundtrip(display);

   usleep( 33000 );

   if ( ctx->surfaceId1 == 0 )
   {
      EMERROR("Did not get surface id for surface 1");
      goto exit;
   }

   memset( &touchSet, 0, sizeof(touchSet) );

   // first touch
   touchSet.touch[0].id= ++touchId;
   touchSet.touch[0].x= 310;
   touchSet.touch[0].y= 110;
   touchSet.touch[0].valid= true;
   touchSet.touch[0].starting= true;
   touchSet.touch[0].stopping= false;
   touchSet.touch[0].moved= false;
   WstCompositorTouchEvent( wctx, &touchSet );

   wl_display_roundtrip(display);

   usleep( 35000 );

   wl_display_roundtrip(display);

   if ( !ctx->touchFrameEnd )
   {
      EMERROR("Missing touch frame");
      goto exit;
   }

   if ( ctx->touchCount != 1 )
   {
      EMERROR("Bad active touch count: expected (%d) actual (%d)", 1, ctx->touchCount );
      goto exit;
   }

   if (
        (ctx->touchSet.touch[0].valid == false) ||
        (ctx->touchSet.touch[0].x != 310) ||
        (ctx->touchSet.touch[0].y != 110)
      )
   {
      EMERROR("Bad touch 1: expected: valid (1 / %d) point (310,110 / %d,%d)",
        ctx->touchSet.touch[0].valid,
        ctx->touchSet.touch[0].x,
        ctx->touchSet.touch[0].y );
      goto exit;
   }

   // first touch moves and second touch starts
   touchSet.touch[0].x= 320;
   touchSet.touch[0].y= 120;
   touchSet.touch[0].valid= true;
   touchSet.touch[0].starting= false;
   touchSet.touch[0].stopping= false;
   touchSet.touch[0].moved= true;

   touchSet.touch[1].id= ++touchId;
   touchSet.touch[1].x= 210;
   touchSet.touch[1].y= 180;
   touchSet.touch[1].valid= true;
   touchSet.touch[1].starting= true;
   touchSet.touch[1].stopping= false;
   touchSet.touch[1].moved= false;
   WstCompositorTouchEvent( wctx, &touchSet );

   wl_display_roundtrip(display);

   usleep( 35000 );

   wl_display_roundtrip(display);

   if ( !ctx->touchFrameEnd )
   {
      EMERROR("Missing touch frame");
      goto exit;
   }

   if ( ctx->touchCount != 2 )
   {
      EMERROR("Bad active touch count: expected (%d) actual (%d)", 2, ctx->touchCount );
      goto exit;
   }

   if (
        (ctx->touchSet.touch[0].valid == false) ||
        (ctx->touchSet.touch[0].x != 320) ||
        (ctx->touchSet.touch[0].y != 120)
      )
   {
      EMERROR("Bad touch 1: expected: valid (1 / %d) point (320,120 / %d,%d)",
        ctx->touchSet.touch[0].valid,
        ctx->touchSet.touch[0].x,
        ctx->touchSet.touch[0].y );
      goto exit;
   }

   if (
        (ctx->touchSet.touch[1].valid == false) ||
        (ctx->touchSet.touch[1].x != 210) ||
        (ctx->touchSet.touch[1].y != 180)
      )
   {
      EMERROR("Bad touch 2: expected: valid (1 / %d) point (210,180 / %d,%d)",
        ctx->touchSet.touch[1].valid,
        ctx->touchSet.touch[1].x,
        ctx->touchSet.touch[1].y );
      goto exit;
   }

   // First touch ends, second touch moves, third touch starts
   touchSet.touch[0].starting= false;
   touchSet.touch[0].stopping= true;

   touchSet.touch[1].x= 211;
   touchSet.touch[1].y= 180;
   touchSet.touch[1].valid= true;
   touchSet.touch[1].starting= false;
   touchSet.touch[1].stopping= false;
   touchSet.touch[1].moved= true;

   touchSet.touch[2].id= ++touchId;
   touchSet.touch[2].x= 400;
   touchSet.touch[2].y= 300;
   touchSet.touch[2].valid= true;
   touchSet.touch[2].starting= true;
   touchSet.touch[2].stopping= false;
   touchSet.touch[2].moved= false;

   WstCompositorTouchEvent( wctx, &touchSet );

   wl_display_roundtrip(display);

   usleep( 35000 );

   wl_display_roundtrip(display);

   if ( ctx->touchCount != 2 )
   {
      EMERROR("Bad active touch count: expected (%d) actual (%d)", 2, ctx->touchCount );
      goto exit;
   }

   if ( ctx->touchSet.touch[0].valid )
   {
      EMERROR("Bad touch1: expected: valid (0 / %d)", ctx->touchSet.touch[0].valid );
      goto exit;
   }

   if (
        (ctx->touchSet.touch[1].valid == false) ||
        (ctx->touchSet.touch[1].x != 211) ||
        (ctx->touchSet.touch[1].y != 180)
      )
   {
      EMERROR("Bad touch 2: expected: valid (1 / %d) point (211,180 / %d,%d)",
        ctx->touchSet.touch[1].valid,
        ctx->touchSet.touch[1].x,
        ctx->touchSet.touch[1].y );
      goto exit;
   }

   if (
        (ctx->touchSet.touch[2].valid == false) ||
        (ctx->touchSet.touch[2].x != 400) ||
        (ctx->touchSet.touch[2].y != 300)
      )
   {
      EMERROR("Bad touch 3: expected: valid (1 / %d) point (400,300 / %d,%d)",
        ctx->touchSet.touch[2].valid,
        ctx->touchSet.touch[2].x,
        ctx->touchSet.touch[2].y );
      goto exit;
   }

   // Second and third touch end
   touchSet.touch[1].starting= false;
   touchSet.touch[1].stopping= true;

   touchSet.touch[2].starting= false;
   touchSet.touch[2].stopping= true;

   WstCompositorTouchEvent( wctx, &touchSet );

   wl_display_roundtrip(display);

   usleep( 35000 );

   wl_display_roundtrip(display);

   if ( ctx->touchCount != 0 )
   {
      EMERROR("Bad active touch count: expected (%d) actual (%d)", 0, ctx->touchCount );
      goto exit;
   }

   testResult= true;

exit:

   if ( ctx->eglSurfaceWindow1 )
   {
      eglDestroySurface( ctx->eglCtx.eglDisplay, ctx->eglSurfaceWindow1 );
      ctx->eglSurfaceWindow1= EGL_NO_SURFACE;
   }

   if ( ctx->wlEglWindow1 )
   {
      wl_egl_window_destroy( ctx->wlEglWindow1 );
      ctx->wlEglWindow1= 0;
   }

   if ( ctx->surface1 )
   {
      wl_surface_destroy( ctx->surface1 );
      ctx->surface1= 0;
   }

   testTermEGL( &ctx->eglCtx );

   if ( ctx->keyboard )
   {
      wl_keyboard_destroy(ctx->keyboard);
      ctx->keyboard= 0;
   }

   if ( ctx->pointer )
   {
      wl_pointer_destroy(ctx->pointer);
      ctx->pointer= 0;
   }

   if ( ctx->seat )
   {
      wl_seat_destroy(ctx->seat);
      ctx->seat= 0;
   }

   if ( ctx->shell )
   {
      wl_simple_shell_destroy(ctx->shell);
      ctx->shell= 0;
   }

   if ( ctx->compositor )
   {
      wl_compositor_destroy( ctx->compositor );
      ctx->compositor= 0;
   }

   if ( registry )
   {
      wl_registry_destroy(registry);
      registry= 0;
   }

   if ( display )
   {
      wl_display_disconnect(display);
      display= 0;
   }

   WstCompositorDestroy( wctxRepeater );

   WstCompositorDestroy( wctx );

   return testResult;
}

bool testCaseTouchBasicFocus( EMCTX *emctx )
{
   using namespace Touch;

   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;
   const char *displayName= "test0";
   struct wl_display *display= 0;
   struct wl_registry *registry= 0;
   TestCtx testCtx;
   TestCtx *ctx= &testCtx;
   EGLBoolean b;
   int touchId= 0;
   WstTouchSet touchSet;
   int gx, gy, gw, gh;

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   memset( &testCtx, 0, sizeof(TestCtx) );

   display= wl_display_connect(displayName);
   if ( !display )
   {
      EMERROR( "wl_display_connect failed" );
      goto exit;
   }
   ctx->display= display;

   registry= wl_display_get_registry(display);
   if ( !registry )
   {
      EMERROR( "wl_display_get_registrty failed" );
      goto exit;
   }

   wl_registry_add_listener(registry, &registryListener, ctx);

   wl_display_roundtrip(display);

   if ( !ctx->compositor || !ctx->seat || !ctx->keyboard || !ctx->pointer || !ctx->touch )
   {
      EMERROR("Failed to acquire needed compositor items");
      goto exit;
   }

   result= testSetupEGL( &ctx->eglCtx, display );
   if ( !result )
   {
      EMERROR("testSetupEGL failed");
      goto exit;
   }

   ctx->windowWidth= WINDOW_WIDTH;
   ctx->windowHeight= WINDOW_HEIGHT;

   eglSwapInterval( ctx->eglCtx.eglDisplay, 1 );



   ctx->surface1= wl_compositor_create_surface(ctx->compositor);
   printf("surface1=%p\n", ctx->surface1);   
   if ( !ctx->surface1 )
   {
      EMERROR("error: unable to create wayland surface");
      goto exit;
   }

   ctx->wlEglWindow1= wl_egl_window_create(ctx->surface1, ctx->windowWidth, ctx->windowHeight);
   if ( !ctx->wlEglWindow1 )
   {
      EMERROR("error: unable to create wl_egl_window");
      goto exit;
   }
   printf("wl_egl_window1 %p\n", ctx->wlEglWindow1);

   ctx->eglSurfaceWindow1= eglCreateWindowSurface( ctx->eglCtx.eglDisplay,
                                                   ctx->eglCtx.eglConfig,
                                                   (EGLNativeWindowType)ctx->wlEglWindow1,
                                                   NULL );
   printf("eglCreateWindowSurface: eglSurfaceWindow1 %p\n", ctx->eglSurfaceWindow1 );

   b= eglMakeCurrent( ctx->eglCtx.eglDisplay, ctx->eglSurfaceWindow1, ctx->eglSurfaceWindow1, ctx->eglCtx.eglContext );
   if ( !b )
   {
      EMERROR("error: eglMakeCurrent failed: %X", eglGetError() );
      goto exit;
   }

   eglSwapBuffers(ctx->eglCtx.eglDisplay, ctx->eglSurfaceWindow1);

   wl_display_roundtrip(display);

   usleep( 33000 );

   if ( ctx->surfaceId1 == 0 )
   {
      EMERROR("Did not get surface id for surface 1");
      goto exit;
   }




   ctx->surface2= wl_compositor_create_surface(ctx->compositor);
   printf("surface2=%p\n", ctx->surface2);   
   if ( !ctx->surface2 )
   {
      EMERROR("error: unable to create wayland surface");
      goto exit;
   }

   ctx->wlEglWindow2= wl_egl_window_create(ctx->surface2, ctx->windowWidth, ctx->windowHeight);
   if ( !ctx->wlEglWindow2 )
   {
      EMERROR("error: unable to create wl_egl_window");
      goto exit;
   }
   printf("wl_egl_window2 %p\n", ctx->wlEglWindow2);

   ctx->eglSurfaceWindow2= eglCreateWindowSurface( ctx->eglCtx.eglDisplay,
                                                   ctx->eglCtx.eglConfig,
                                                   (EGLNativeWindowType)ctx->wlEglWindow2,
                                                   NULL );
   printf("eglCreateWindowSurface: eglSurfaceWindow2 %p\n", ctx->eglSurfaceWindow2 );

   b= eglMakeCurrent( ctx->eglCtx.eglDisplay, ctx->eglSurfaceWindow2, ctx->eglSurfaceWindow2, ctx->eglCtx.eglContext );
   if ( !b )
   {
      EMERROR("error: eglMakeCurrent failed: %X", eglGetError() );
      goto exit;
   }

   eglSwapBuffers(ctx->eglCtx.eglDisplay, ctx->eglSurfaceWindow2);

   wl_display_roundtrip(display);

   usleep( 33000 );

   if ( ctx->surfaceId2 == 0 )
   {
      EMERROR("Did not get surface id for surface 2");
      goto exit;
   }

   // Position surfaces to be side-by-side
   gx= 10;
   gy= 10;
   gw= 200;
   gh= 200;
   wl_simple_shell_set_geometry( ctx->shell, ctx->surfaceId1, gx, gy, gw, gh);

   wl_display_roundtrip(display);

   gx= 210;
   gy= 10;
   gw= 200;
   gh= 200;
   wl_simple_shell_set_geometry( ctx->shell, ctx->surfaceId2, gx, gy, gw, gh);

   wl_display_roundtrip(display);


   // Key input focus
   memset( &touchSet, 0, sizeof(touchSet) );

   touchSet.touch[0].id= ++touchId;
   touchSet.touch[0].x= 310;
   touchSet.touch[0].y= 110;
   touchSet.touch[0].valid= true;
   touchSet.touch[0].starting= true;
   touchSet.touch[0].stopping= false;
   touchSet.touch[0].moved= false;
   WstCompositorTouchEvent( wctx, &touchSet );

   touchSet.touch[0].starting= false;
   touchSet.touch[0].stopping= true;
   WstCompositorTouchEvent( wctx, &touchSet );

   wl_display_roundtrip(display);

   usleep( 17000 );

   wl_display_roundtrip(display);

   WstCompositorKeyEvent( wctx,  KEY_D, WstKeyboard_keyState_depressed, 0 );

   wl_display_roundtrip(display);

   usleep( 35000 );

   wl_display_roundtrip(display);

   if ( (ctx->surfaceWithKeyInput != ctx->surface2) || (ctx->keyPressed != KEY_D) )
   {
      EMERROR("Failed to get key input with expected surface");
      goto exit;
   }

   WstCompositorKeyEvent( wctx,  KEY_D, WstKeyboard_keyState_released, 0 );

   wl_display_roundtrip(display);

   usleep( 35000 );

   wl_display_roundtrip(display);



   memset( &touchSet, 0, sizeof(touchSet) );

   touchSet.touch[0].id= ++touchId;
   touchSet.touch[0].x= 110;
   touchSet.touch[0].y= 110;
   touchSet.touch[0].valid= true;
   touchSet.touch[0].starting= true;
   touchSet.touch[0].stopping= false;
   touchSet.touch[0].moved= false;
   WstCompositorTouchEvent( wctx, &touchSet );

   touchSet.touch[0].starting= false;
   touchSet.touch[0].stopping= true;
   WstCompositorTouchEvent( wctx, &touchSet );

   wl_display_roundtrip(display);

   WstCompositorKeyEvent( wctx,  KEY_J, WstKeyboard_keyState_depressed, 0 );

   wl_display_roundtrip(display);

   usleep( 35000 );

   wl_display_roundtrip(display);

   if ( (ctx->surfaceWithKeyInput != ctx->surface1) || (ctx->keyPressed != KEY_J) )
   {
      EMERROR("Failed to get key input with expected surface");
      goto exit;
   }

   WstCompositorKeyEvent( wctx,  KEY_J, WstKeyboard_keyState_released, 0 );

   wl_display_roundtrip(display);

   usleep( 35000 );

   wl_display_roundtrip(display);

   if ( EMGetWaylandThreadingIssue( emctx ) )
   {
      EMERROR( "Wayland threading issue: compositor calling wl_resource_post_event_array from multiple threads") ;
      goto exit;
   }

   testResult= true;

exit:

   if ( ctx->eglSurfaceWindow2 )
   {
      eglDestroySurface( ctx->eglCtx.eglDisplay, ctx->eglSurfaceWindow2 );
      ctx->eglSurfaceWindow2= EGL_NO_SURFACE;
   }

   if ( ctx->wlEglWindow2 )
   {
      wl_egl_window_destroy( ctx->wlEglWindow2 );
      ctx->wlEglWindow2= 0;
   }

   if ( ctx->surface2 )
   {
      wl_surface_destroy( ctx->surface2 );
      ctx->surface2= 0;
   }

   if ( ctx->eglSurfaceWindow1 )
   {
      eglDestroySurface( ctx->eglCtx.eglDisplay, ctx->eglSurfaceWindow1 );
      ctx->eglSurfaceWindow1= EGL_NO_SURFACE;
   }

   if ( ctx->wlEglWindow1 )
   {
      wl_egl_window_destroy( ctx->wlEglWindow1 );
      ctx->wlEglWindow1= 0;
   }

   if ( ctx->surface1 )
   {
      wl_surface_destroy( ctx->surface1 );
      ctx->surface1= 0;
   }

   testTermEGL( &ctx->eglCtx );

   if ( ctx->keyboard )
   {
      wl_keyboard_destroy(ctx->keyboard);
      ctx->keyboard= 0;
   }

   if ( ctx->pointer )
   {
      wl_pointer_destroy(ctx->pointer);
      ctx->pointer= 0;
   }

   if ( ctx->seat )
   {
      wl_seat_destroy(ctx->seat);
      ctx->seat= 0;
   }

   if ( ctx->shell )
   {
      wl_simple_shell_destroy(ctx->shell);
      ctx->shell= 0;
   }

   if ( ctx->compositor )
   {
      wl_compositor_destroy( ctx->compositor );
      ctx->compositor= 0;
   }

   if ( registry )
   {
      wl_registry_destroy(registry);
      registry= 0;
   }

   if ( display )
   {
      wl_display_disconnect(display);
      display= 0;
   }

   WstCompositorDestroy( wctx );

   return testResult;
}

bool testCaseTouchBasicFocusRepeater( EMCTX *emctx )
{
   using namespace Touch;

   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;
   WstCompositor *wctxRepeater= 0;
   const char *displayName= "test0";
   const char *displayNameNested= "nested0";
   struct wl_display *display= 0;
   struct wl_registry *registry= 0;
   TestCtx testCtx;
   TestCtx *ctx= &testCtx;
   EGLBoolean b;
   int touchId= 0;
   WstTouchSet touchSet;
   int gx, gy, gw, gh;

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }


   wctxRepeater= WstCompositorCreate();
   if ( !wctxRepeater )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctxRepeater, displayNameNested );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetIsRepeater( wctxRepeater, true );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetIsNested failed" );
      goto exit;
   }

   result= WstCompositorSetNestedDisplayName( wctxRepeater, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorStart( wctxRepeater );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }


   memset( &testCtx, 0, sizeof(TestCtx) );

   display= wl_display_connect(displayNameNested);
   if ( !display )
   {
      EMERROR( "wl_display_connect failed" );
      goto exit;
   }
   ctx->display= display;

   registry= wl_display_get_registry(display);
   if ( !registry )
   {
      EMERROR( "wl_display_get_registrty failed" );
      goto exit;
   }

   wl_registry_add_listener(registry, &registryListener, ctx);

   wl_display_roundtrip(display);

   if ( !ctx->compositor || !ctx->seat || !ctx->keyboard || !ctx->pointer || !ctx->touch )
   {
      EMERROR("Failed to acquire needed compositor items");
      goto exit;
   }

   result= testSetupEGL( &ctx->eglCtx, display );
   if ( !result )
   {
      EMERROR("testSetupEGL failed");
      goto exit;
   }

   ctx->windowWidth= WINDOW_WIDTH;
   ctx->windowHeight= WINDOW_HEIGHT;

   eglSwapInterval( ctx->eglCtx.eglDisplay, 1 );



   ctx->surface1= wl_compositor_create_surface(ctx->compositor);
   printf("surface1=%p\n", ctx->surface1);   
   if ( !ctx->surface1 )
   {
      EMERROR("error: unable to create wayland surface");
      goto exit;
   }

   ctx->wlEglWindow1= wl_egl_window_create(ctx->surface1, ctx->windowWidth, ctx->windowHeight);
   if ( !ctx->wlEglWindow1 )
   {
      EMERROR("error: unable to create wl_egl_window");
      goto exit;
   }
   printf("wl_egl_window1 %p\n", ctx->wlEglWindow1);

   ctx->eglSurfaceWindow1= eglCreateWindowSurface( ctx->eglCtx.eglDisplay,
                                                   ctx->eglCtx.eglConfig,
                                                   (EGLNativeWindowType)ctx->wlEglWindow1,
                                                   NULL );
   printf("eglCreateWindowSurface: eglSurfaceWindow1 %p\n", ctx->eglSurfaceWindow1 );

   b= eglMakeCurrent( ctx->eglCtx.eglDisplay, ctx->eglSurfaceWindow1, ctx->eglSurfaceWindow1, ctx->eglCtx.eglContext );
   if ( !b )
   {
      EMERROR("error: eglMakeCurrent failed: %X", eglGetError() );
      goto exit;
   }

   eglSwapBuffers(ctx->eglCtx.eglDisplay, ctx->eglSurfaceWindow1);

   wl_display_roundtrip(display);

   usleep( 33000 );

   if ( ctx->surfaceId1 == 0 )
   {
      EMERROR("Did not get surface id for surface 1");
      goto exit;
   }




   ctx->surface2= wl_compositor_create_surface(ctx->compositor);
   printf("surface2=%p\n", ctx->surface2);   
   if ( !ctx->surface2 )
   {
      EMERROR("error: unable to create wayland surface");
      goto exit;
   }

   ctx->wlEglWindow2= wl_egl_window_create(ctx->surface2, ctx->windowWidth, ctx->windowHeight);
   if ( !ctx->wlEglWindow2 )
   {
      EMERROR("error: unable to create wl_egl_window");
      goto exit;
   }
   printf("wl_egl_window2 %p\n", ctx->wlEglWindow2);

   ctx->eglSurfaceWindow2= eglCreateWindowSurface( ctx->eglCtx.eglDisplay,
                                                   ctx->eglCtx.eglConfig,
                                                   (EGLNativeWindowType)ctx->wlEglWindow2,
                                                   NULL );
   printf("eglCreateWindowSurface: eglSurfaceWindow2 %p\n", ctx->eglSurfaceWindow2 );

   b= eglMakeCurrent( ctx->eglCtx.eglDisplay, ctx->eglSurfaceWindow2, ctx->eglSurfaceWindow2, ctx->eglCtx.eglContext );
   if ( !b )
   {
      EMERROR("error: eglMakeCurrent failed: %X", eglGetError() );
      goto exit;
   }

   eglSwapBuffers(ctx->eglCtx.eglDisplay, ctx->eglSurfaceWindow2);

   wl_display_roundtrip(display);

   usleep( 33000 );

   if ( ctx->surfaceId2 == 0 )
   {
      EMERROR("Did not get surface id for surface 2");
      goto exit;
   }

   // Position surfaces to be side-by-side
   gx= 10;
   gy= 10;
   gw= 200;
   gh= 200;
   wl_simple_shell_set_geometry( ctx->shell, ctx->surfaceId1, gx, gy, gw, gh);

   wl_display_roundtrip(display);

   gx= 210;
   gy= 10;
   gw= 200;
   gh= 200;
   wl_simple_shell_set_geometry( ctx->shell, ctx->surfaceId2, gx, gy, gw, gh);

   wl_display_roundtrip(display);


   // Key input focus
   memset( &touchSet, 0, sizeof(touchSet) );

   touchSet.touch[0].id= ++touchId;
   touchSet.touch[0].x= 310;
   touchSet.touch[0].y= 110;
   touchSet.touch[0].valid= true;
   touchSet.touch[0].starting= true;
   touchSet.touch[0].stopping= false;
   touchSet.touch[0].moved= false;
   WstCompositorTouchEvent( wctx, &touchSet );

   touchSet.touch[0].starting= false;
   touchSet.touch[0].stopping= true;
   WstCompositorTouchEvent( wctx, &touchSet );

   wl_display_roundtrip(display);

   usleep( 17000 );

   wl_display_roundtrip(display);

   WstCompositorKeyEvent( wctx,  KEY_D, WstKeyboard_keyState_depressed, 0 );

   wl_display_roundtrip(display);

   usleep( 35000 );

   wl_display_roundtrip(display);

   if ( (ctx->surfaceWithKeyInput != ctx->surface2) || (ctx->keyPressed != KEY_D) )
   {
      EMERROR("Failed to get key input with expected surface");
      goto exit;
   }

   WstCompositorKeyEvent( wctx,  KEY_D, WstKeyboard_keyState_released, 0 );

   wl_display_roundtrip(display);

   usleep( 35000 );

   wl_display_roundtrip(display);



   memset( &touchSet, 0, sizeof(touchSet) );

   touchSet.touch[0].id= ++touchId;
   touchSet.touch[0].x= 110;
   touchSet.touch[0].y= 110;
   touchSet.touch[0].valid= true;
   touchSet.touch[0].starting= true;
   touchSet.touch[0].stopping= false;
   touchSet.touch[0].moved= false;
   WstCompositorTouchEvent( wctx, &touchSet );

   touchSet.touch[0].starting= false;
   touchSet.touch[0].stopping= true;
   WstCompositorTouchEvent( wctx, &touchSet );

   wl_display_roundtrip(display);

   WstCompositorKeyEvent( wctx,  KEY_J, WstKeyboard_keyState_depressed, 0 );

   wl_display_roundtrip(display);

   usleep( 35000 );

   wl_display_roundtrip(display);

   if ( (ctx->surfaceWithKeyInput != ctx->surface1) || (ctx->keyPressed != KEY_J) )
   {
      EMERROR("Failed to get key input with expected surface");
      goto exit;
   }

   WstCompositorKeyEvent( wctx,  KEY_J, WstKeyboard_keyState_released, 0 );

   wl_display_roundtrip(display);

   usleep( 35000 );

   wl_display_roundtrip(display);

   testResult= true;

exit:

   if ( ctx->eglSurfaceWindow2 )
   {
      eglDestroySurface( ctx->eglCtx.eglDisplay, ctx->eglSurfaceWindow2 );
      ctx->eglSurfaceWindow2= EGL_NO_SURFACE;
   }

   if ( ctx->wlEglWindow2 )
   {
      wl_egl_window_destroy( ctx->wlEglWindow2 );
      ctx->wlEglWindow2= 0;
   }

   if ( ctx->surface2 )
   {
      wl_surface_destroy( ctx->surface2 );
      ctx->surface2= 0;
   }

   if ( ctx->eglSurfaceWindow1 )
   {
      eglDestroySurface( ctx->eglCtx.eglDisplay, ctx->eglSurfaceWindow1 );
      ctx->eglSurfaceWindow1= EGL_NO_SURFACE;
   }

   if ( ctx->wlEglWindow1 )
   {
      wl_egl_window_destroy( ctx->wlEglWindow1 );
      ctx->wlEglWindow1= 0;
   }

   if ( ctx->surface1 )
   {
      wl_surface_destroy( ctx->surface1 );
      ctx->surface1= 0;
   }

   testTermEGL( &ctx->eglCtx );

   if ( ctx->keyboard )
   {
      wl_keyboard_destroy(ctx->keyboard);
      ctx->keyboard= 0;
   }

   if ( ctx->pointer )
   {
      wl_pointer_destroy(ctx->pointer);
      ctx->pointer= 0;
   }

   if ( ctx->seat )
   {
      wl_seat_destroy(ctx->seat);
      ctx->seat= 0;
   }

   if ( ctx->shell )
   {
      wl_simple_shell_destroy(ctx->shell);
      ctx->shell= 0;
   }

   if ( ctx->compositor )
   {
      wl_compositor_destroy( ctx->compositor );
      ctx->compositor= 0;
   }

   if ( registry )
   {
      wl_registry_destroy(registry);
      registry= 0;
   }

   if ( display )
   {
      wl_display_disconnect(display);
      display= 0;
   }

   WstCompositorDestroy( wctxRepeater );

   WstCompositorDestroy( wctx );

   return testResult;
}

