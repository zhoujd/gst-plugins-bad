/*
 * GStreamer
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
 * Copyright (C) 2012 Matthew Waters <ystreet00@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "wayland_event_source.h"

#include "gstglwindow_wayland_egl.h"

const gchar *WlEGLErrorString ();

#define GST_CAT_DEFAULT gst_gl_window_debug

#define gst_gl_window_wayland_egl_parent_class parent_class
G_DEFINE_TYPE (GstGLWindowWaylandEGL, gst_gl_window_wayland_egl,
    GST_GL_TYPE_WINDOW);

static guintptr gst_gl_window_wayland_egl_get_gl_context (GstGLWindow * window);
static gboolean gst_gl_window_wayland_egl_activate (GstGLWindow * window,
    gboolean activate);
static void gst_gl_window_wayland_egl_set_window_handle (GstGLWindow * window,
    guintptr handle);
static void gst_gl_window_wayland_egl_draw (GstGLWindow * window, guint width,
    guint height);
static void gst_gl_window_wayland_egl_run (GstGLWindow * window);
static void gst_gl_window_wayland_egl_quit (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data);
static void gst_gl_window_wayland_egl_send_message (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data);
static void gst_gl_window_wayland_egl_destroy_context (GstGLWindowWaylandEGL *
    window_egl);
static gboolean gst_gl_window_wayland_egl_create_context (GstGLWindow
    * window, GstGLAPI gl_api, guintptr external_gl_context, GError ** error);
static GstGLAPI gst_gl_window_wayland_egl_get_gl_api (GstGLWindow * window);
static gpointer gst_gl_window_wayland_egl_get_proc_address (GstGLWindow *
    window, const gchar * name);
static void gst_gl_window_wayland_egl_close (GstGLWindow * window);

static void
pointer_handle_enter (void *data, struct wl_pointer *pointer, uint32_t serial,
    struct wl_surface *surface, wl_fixed_t sx_w, wl_fixed_t sy_w)
{
  GstGLWindowWaylandEGL *window_egl = data;
  window_egl->display.serial = serial;
}

static void
pointer_handle_leave (void *data, struct wl_pointer *pointer, uint32_t serial,
    struct wl_surface *surface)
{
  GstGLWindowWaylandEGL *window_egl = data;
  window_egl->display.serial = serial;
}

static void
pointer_handle_motion (void *data, struct wl_pointer *pointer, uint32_t time,
    wl_fixed_t sx_w, wl_fixed_t sy_w)
{
}

static void
pointer_handle_button (void *data, struct wl_pointer *pointer, uint32_t serial,
    uint32_t time, uint32_t button, uint32_t state_w)
{
  GstGLWindowWaylandEGL *window_egl = data;
  window_egl->display.serial = serial;
}

static void
pointer_handle_axis (void *data, struct wl_pointer *pointer, uint32_t time,
    uint32_t axis, wl_fixed_t value)
{
}

static const struct wl_pointer_listener pointer_listener = {
  pointer_handle_enter,
  pointer_handle_leave,
  pointer_handle_motion,
  pointer_handle_button,
  pointer_handle_axis,
};

static void
seat_handle_capabilities (void *data, struct wl_seat *seat,
    enum wl_seat_capability caps)
{
  GstGLWindowWaylandEGL *window_egl = data;
  struct display *display = &window_egl->display;

  if ((caps & WL_SEAT_CAPABILITY_POINTER) && !display->pointer) {
    display->pointer = wl_seat_get_pointer (seat);
    wl_pointer_set_user_data (display->pointer, window_egl);
    wl_pointer_add_listener (display->pointer, &pointer_listener, window_egl);
  } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && display->pointer) {
    wl_pointer_destroy (display->pointer);
    display->pointer = NULL;
  }
#if 0
  if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !input->keyboard) {
    input->keyboard = wl_seat_get_keyboard (seat);
    wl_keyboard_set_user_data (input->keyboard, input);
    wl_keyboard_add_listener (input->keyboard, &keyboard_listener, input);
  } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && input->keyboard) {
    wl_keyboard_destroy (input->keyboard);
    input->keyboard = NULL;
  }
#endif
}

static const struct wl_seat_listener seat_listener = {
  seat_handle_capabilities,
};

static void
handle_ping (void *data, struct wl_shell_surface *shell_surface,
    uint32_t serial)
{
  wl_shell_surface_pong (shell_surface, serial);
}

static void window_resize (GstGLWindowWaylandEGL * window_egl, guint width,
    guint height);

static void
handle_configure (void *data, struct wl_shell_surface *shell_surface,
    uint32_t edges, int32_t width, int32_t height)
{
  GstGLWindowWaylandEGL *window_egl = data;
  GstGLWindow *window = GST_GL_WINDOW (window_egl);

  GST_DEBUG ("configure event %ix%i", width, height);

  window_resize (window_egl, width, height);

  if (window->resize)
    window->resize (window->resize_data, width, height);
}

static void
handle_popup_done (void *data, struct wl_shell_surface *shell_surface)
{
}

static const struct wl_shell_surface_listener shell_surface_listener = {
  handle_ping,
  handle_configure,
  handle_popup_done
};

static gboolean
create_surface (GstGLWindowWaylandEGL * window_egl)
{
  window_egl->window.surface =
      wl_compositor_create_surface (window_egl->display.compositor);
  window_egl->window.shell_surface =
      wl_shell_get_shell_surface (window_egl->display.shell,
      window_egl->window.surface);

  wl_shell_surface_add_listener (window_egl->window.shell_surface,
      &shell_surface_listener, window_egl);

  if (window_egl->window.window_width <= 0)
    window_egl->window.window_width = 320;
  if (window_egl->window.window_height <= 0)
    window_egl->window.window_height = 240;

  window_egl->window.native =
      wl_egl_window_create (window_egl->window.surface,
      window_egl->window.window_width, window_egl->window.window_height);

  wl_shell_surface_set_title (window_egl->window.shell_surface,
      "OpenGL Renderer");

  wl_shell_surface_set_toplevel (window_egl->window.shell_surface);

  wl_shell_surface_set_fullscreen (window_egl->window.shell_surface,
      WL_SHELL_SURFACE_FULLSCREEN_METHOD_SCALE, 0, NULL);

  return TRUE;
}

static void
destroy_surface (GstGLWindowWaylandEGL * window_egl)
{
  if (window_egl->window.native)
    wl_egl_window_destroy (window_egl->window.native);

  if (window_egl->window.shell_surface)
    wl_shell_surface_destroy (window_egl->window.shell_surface);

  if (window_egl->window.surface)
    wl_surface_destroy (window_egl->window.surface);

  if (window_egl->window.callback)
    wl_callback_destroy (window_egl->window.callback);
}

static void
registry_handle_global (void *data, struct wl_registry *registry,
    uint32_t name, const char *interface, uint32_t version)
{
  struct display *d = data;

  if (g_strcmp0 (interface, "wl_compositor") == 0) {
    d->compositor =
        wl_registry_bind (registry, name, &wl_compositor_interface, 1);
  } else if (g_strcmp0 (interface, "wl_shell") == 0) {
    d->shell = wl_registry_bind (registry, name, &wl_shell_interface, 1);
  } else if (g_strcmp0 (interface, "wl_seat") == 0) {
    d->seat = wl_registry_bind (registry, name, &wl_seat_interface, 1);
    wl_seat_add_listener (d->seat, &seat_listener, d);
  } else if (g_strcmp0 (interface, "wl_shm") == 0) {
    d->shm = wl_registry_bind (registry, name, &wl_shm_interface, 1);
    d->cursor_theme = wl_cursor_theme_load (NULL, 32, d->shm);
    d->default_cursor =
        wl_cursor_theme_get_cursor (d->cursor_theme, "left_ptr");
  }
}

static const struct wl_registry_listener registry_listener = {
  registry_handle_global
};

static void
gst_gl_window_wayland_egl_class_init (GstGLWindowWaylandEGLClass * klass)
{
  GstGLWindowClass *window_class = (GstGLWindowClass *) klass;

  window_class->create_context =
      GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_create_context);
  window_class->get_gl_context =
      GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_get_gl_context);
  window_class->activate =
      GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_activate);
  window_class->set_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_set_window_handle);
  window_class->draw_unlocked =
      GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_draw);
  window_class->draw = GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_draw);
  window_class->run = GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_run);
  window_class->quit = GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_quit);
  window_class->send_message =
      GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_send_message);
  window_class->get_gl_api =
      GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_get_gl_api);
  window_class->get_proc_address =
      GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_get_proc_address);
  window_class->close = GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_close);
}

static void
gst_gl_window_wayland_egl_init (GstGLWindowWaylandEGL * window)
{
}

/* Must be called in the gl thread */
GstGLWindowWaylandEGL *
gst_gl_window_wayland_egl_new (void)
{
  GstGLWindowWaylandEGL *window;

  GST_DEBUG ("creating Wayland EGL window");

  window = g_object_new (GST_GL_TYPE_WINDOW_WAYLAND_EGL, NULL);

  gst_gl_window_set_need_lock (GST_GL_WINDOW (window), FALSE);

  return window;
}

static void
gst_gl_window_wayland_egl_close (GstGLWindow * window)
{
  GstGLWindowWaylandEGL *window_egl;

  window_egl = GST_GL_WINDOW_WAYLAND_EGL (window);

  gst_gl_window_wayland_egl_destroy_context (window_egl);

  if (window_egl->display.cursor_surface)
    wl_surface_destroy (window_egl->display.cursor_surface);

  if (window_egl->display.cursor_theme)
    wl_cursor_theme_destroy (window_egl->display.cursor_theme);

  if (window_egl->display.shell)
    wl_shell_destroy (window_egl->display.shell);

  if (window_egl->display.compositor)
    wl_compositor_destroy (window_egl->display.compositor);

  if (window_egl->display.display) {
    wl_display_flush (window_egl->display.display);
    wl_display_disconnect (window_egl->display.display);
  }
}

static gboolean
_setup_wayland (GstGLWindowWaylandEGL * window_egl, GError ** error)
{
  window_egl->display.display = wl_display_connect (NULL);
  if (!window_egl->display.display) {
    g_set_error (error, GST_GL_WINDOW_ERROR,
        GST_GL_WINDOW_ERROR_RESOURCE_UNAVAILABLE,
        "Failed to connect to Wayland display server");
    goto error;
  }

  window_egl->display.registry =
      wl_display_get_registry (window_egl->display.display);
  wl_registry_add_listener (window_egl->display.registry, &registry_listener,
      &window_egl->display);

  wl_display_dispatch (window_egl->display.display);

  create_surface (window_egl);

  window_egl->display.cursor_surface =
      wl_compositor_create_surface (window_egl->display.compositor);

  window_egl->wl_source =
      wayland_event_source_new (window_egl->display.display);
  window_egl->main_context = g_main_context_new ();
  window_egl->loop = g_main_loop_new (window_egl->main_context, FALSE);

  g_source_attach (window_egl->wl_source, window_egl->main_context);

  return TRUE;

error:
  return FALSE;
}

gboolean
gst_gl_window_wayland_egl_create_context (GstGLWindow * window,
    GstGLAPI gl_api, guintptr external_gl_context, GError ** error)
{
  GstGLWindowWaylandEGL *window_egl = GST_GL_WINDOW_WAYLAND_EGL (window);
  if (!_setup_wayland (window_egl, error))
    return FALSE;

  window_egl->egl =
      gst_gl_egl_create_context (eglGetDisplay ((EGLNativeDisplayType)
          window_egl->display.display), window_egl->window.native, gl_api,
      external_gl_context, error);
  if (!window_egl->egl)
    goto failure;

  return TRUE;

failure:
  return FALSE;
}

static void
gst_gl_window_wayland_egl_destroy_context (GstGLWindowWaylandEGL * window_egl)
{
  gst_gl_egl_activate (window_egl->egl, FALSE);

  destroy_surface (window_egl);
  /*
     gst_gl_egl_destroy_surface (window_egl->egl);
   */
  gst_gl_egl_destroy_context (window_egl->egl);
  window_egl->egl = NULL;
}

static gboolean
gst_gl_window_wayland_egl_activate (GstGLWindow * window, gboolean activate)
{
  GstGLWindowWaylandEGL *window_egl;

  window_egl = GST_GL_WINDOW_WAYLAND_EGL (window);

  return gst_gl_egl_activate (window_egl->egl, activate);
}

static guintptr
gst_gl_window_wayland_egl_get_gl_context (GstGLWindow * window)
{
  GstGLWindowWaylandEGL *window_egl;

  window_egl = GST_GL_WINDOW_WAYLAND_EGL (window);

  return gst_gl_egl_get_gl_context (window_egl->egl);
}

static GstGLAPI
gst_gl_window_wayland_egl_get_gl_api (GstGLWindow * window)
{
  GstGLWindowWaylandEGL *window_egl;

  window_egl = GST_GL_WINDOW_WAYLAND_EGL (window);

  return gst_gl_egl_get_gl_api (window_egl->egl);
}

static void
gst_gl_window_wayland_egl_swap_buffers (GstGLWindow * window)
{
  GstGLWindowWaylandEGL *window_egl = GST_GL_WINDOW_WAYLAND_EGL (window);

  gst_gl_egl_swap_buffers (window_egl->egl);
}

static void
gst_gl_window_wayland_egl_run (GstGLWindow * window)
{
  GstGLWindowWaylandEGL *window_egl;

  window_egl = GST_GL_WINDOW_WAYLAND_EGL (window);

  GST_LOG ("starting main loop");
  g_main_loop_run (window_egl->loop);
  GST_LOG ("exiting main loop");
}

static void
gst_gl_window_wayland_egl_quit (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  GstGLWindowWaylandEGL *window_egl;

  window_egl = GST_GL_WINDOW_WAYLAND_EGL (window);

  if (callback)
    gst_gl_window_wayland_egl_send_message (window, callback, data);

  GST_LOG ("sending quit");

  g_main_loop_quit (window_egl->loop);

  GST_LOG ("quit sent");
}

typedef struct _GstGLMessage
{
  GMutex lock;
  GCond cond;
  gboolean fired;

  GstGLWindowCB callback;
  gpointer data;
} GstGLMessage;

static gboolean
_run_message (GstGLMessage * message)
{
  g_mutex_lock (&message->lock);

  if (message->callback)
    message->callback (message->data);

  message->fired = TRUE;
  g_cond_signal (&message->cond);
  g_mutex_unlock (&message->lock);

  return FALSE;
}

static void
gst_gl_window_wayland_egl_send_message (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data)
{
  GstGLWindowWaylandEGL *window_egl;
  GstGLMessage message;

  window_egl = GST_GL_WINDOW_WAYLAND_EGL (window);
  message.callback = callback;
  message.data = data;
  message.fired = FALSE;
  g_mutex_init (&message.lock);
  g_cond_init (&message.cond);

  g_main_context_invoke (window_egl->main_context, (GSourceFunc) _run_message,
      &message);

  g_mutex_lock (&message.lock);

  while (!message.fired)
    g_cond_wait (&message.cond, &message.lock);
  g_mutex_unlock (&message.lock);
}

static void
gst_gl_window_wayland_egl_set_window_handle (GstGLWindow * window,
    guintptr handle)
{
}

static void
window_resize (GstGLWindowWaylandEGL * window_egl, guint width, guint height)
{
  GST_DEBUG ("resizing window from %ux%u to %ux%u",
      window_egl->window.window_width, window_egl->window.window_height, width,
      height);

  if (window_egl->window.native) {
    wl_egl_window_resize (window_egl->window.native, width, height, 0, 0);
  }

  window_egl->window.window_width = width;
  window_egl->window.window_height = height;

#if 0
  wl_shell_surface_resize (window_egl->window.shell_surface,
      window_egl->display.seat, window_egl->display.serial, 0);
#endif
}

struct draw
{
  GstGLWindowWaylandEGL *window;
  guint width, height;
};

static void
draw_cb (gpointer data)
{
  struct draw *draw_data = data;
  GstGLWindowWaylandEGL *window_egl = draw_data->window;
  GstGLWindow *window = GST_GL_WINDOW (window_egl);

  if (window_egl->window.window_width != draw_data->width
      || window_egl->window.window_height != draw_data->height) {
    GST_DEBUG ("dimensions don't match, attempting resize");
#if 0
    window_resize (window_egl, draw_data->width, draw_data->height);
#endif
  }

  if (window->draw)
    window->draw (window->draw_data);

  gst_gl_window_wayland_egl_swap_buffers (window);
}

static void
gst_gl_window_wayland_egl_draw (GstGLWindow * window, guint width, guint height)
{
  struct draw draw_data;

  draw_data.window = GST_GL_WINDOW_WAYLAND_EGL (window);
  draw_data.width = width;
  draw_data.height = height;

  gst_gl_window_send_message (window, (GstGLWindowCB) draw_cb, &draw_data);
}

static gpointer
gst_gl_window_wayland_egl_get_proc_address (GstGLWindow * window,
    const gchar * name)
{
  GstGLWindowWaylandEGL *window_egl;
  gpointer result;

  window_egl = GST_GL_WINDOW_WAYLAND_EGL (window);

  if (!(result = gst_gl_egl_get_proc_address (window_egl->egl, name))) {
    result = gst_gl_window_default_get_proc_address (window, name);
  }

  return result;
}
