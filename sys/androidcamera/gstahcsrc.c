/* GStreamer android.hardware.Camera Source
 *
 * Copyright (C) 2012, Cisco Systems, Inc.
 *   Author: Youness Alaoui <youness.alaoui@collabora.co.uk>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/video/video.h>

#include "gstahcsrc.h"
#include "gst-dvm.h"

static void gst_ahc_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_ahc_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_ahc_src_dispose (GObject * object);

static GstStateChangeReturn gst_ahc_src_change_state (GstElement * element,
    GstStateChange transition);
static GstCaps *gst_ahc_src_getcaps (GstBaseSrc * src);
static gboolean gst_ahc_src_start (GstBaseSrc * bsrc);
static gboolean gst_ahc_src_stop (GstBaseSrc * bsrc);
static gboolean gst_ahc_src_unlock (GstBaseSrc * bsrc);
static gboolean gst_ahc_src_unlock_stop (GstBaseSrc * bsrc);
static GstFlowReturn gst_ahc_src_create (GstPushSrc * src, GstBuffer ** buffer);

#define GST_AHC_SRC_CAPS_STR                                    \
  GST_VIDEO_CAPS_YUV (" { YV12 , YUY2 , NV21 , NV16 }") ";"     \
  GST_VIDEO_CAPS_RGB_16

static GstStaticPadTemplate gst_ahc_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AHC_SRC_CAPS_STR));

GST_DEBUG_CATEGORY_STATIC (gst_ahc_src_debug);
#define GST_CAT_DEFAULT gst_ahc_src_debug

enum
{
  ARG_0,
};
GST_BOILERPLATE (GstAHCSrc, gst_ahc_src, GstPushSrc, GST_TYPE_PUSH_SRC);

static void
gst_ahc_src_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG_CATEGORY_INIT (gst_ahc_src_debug, "ahcsrc", 0,
      "android.hardware.Camera source element");

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_ahc_src_pad_template);
  gst_element_class_set_details_simple (gstelement_class,
      "Android Camera Source",
      "Source/Video",
      "Reads frames from android.hardware.Camera class into buffers",
      "Youness Alaoui <youness.alaoui@collabora.co.uk>");
}

static void
gst_ahc_src_class_init (GstAHCSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);
  GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);

  gobject_class->set_property = gst_ahc_src_set_property;
  gobject_class->get_property = gst_ahc_src_get_property;
  gobject_class->dispose = gst_ahc_src_dispose;

  element_class->change_state = gst_ahc_src_change_state;

  gstbasesrc_class->get_caps = gst_ahc_src_getcaps;
  gstbasesrc_class->start = gst_ahc_src_start;
  gstbasesrc_class->stop = gst_ahc_src_stop;
  gstbasesrc_class->unlock = gst_ahc_src_unlock;
  gstbasesrc_class->unlock_stop = gst_ahc_src_unlock_stop;

  gstpushsrc_class->create = gst_ahc_src_create;
}

static void
gst_ahc_src_init (GstAHCSrc * self, GstAHCSrcClass * klass)
{
  gst_base_src_set_live (GST_BASE_SRC (self), TRUE);
  gst_base_src_set_format (GST_BASE_SRC (self), GST_FORMAT_TIME);
  gst_base_src_set_do_timestamp (GST_BASE_SRC (self), TRUE);

  self->camera = NULL;
  self->texture = gst_ag_surfacetexture_new (0);
  self->data = NULL;
  self->queue = g_async_queue_new ();
  self->caps = NULL;
  self->flushing = FALSE;
}

static void
gst_ahc_src_dispose (GObject * object)
{
  GstAHCSrc *self = GST_AHC_SRC (object);

  if (self->camera)
    gst_ah_camera_release (self->camera);
  self->camera = NULL;

  if (self->texture)
    gst_ag_surfacetexture_release (self->texture);
  self->texture = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstCaps *
gst_ahc_src_getcaps (GstBaseSrc * src)
{
  GstAHCSrc *self = GST_AHC_SRC (src);

  return gst_caps_copy (self->caps);
}

static void
gst_ahc_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAHCSrc *self = GST_AHC_SRC (object);
  (void) self;

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ahc_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAHCSrc *self = GST_AHC_SRC (object);
  (void) self;

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ahc_src_on_preview_frame (jbyteArray data, gpointer user_data)
{
  GstAHCSrc *self = GST_AHC_SRC (user_data);
  JNIEnv *env = gst_dvm_get_env ();

  //GST_WARNING_OBJECT (self, "Received data buffer %p", data);
  g_async_queue_push (self->queue, (*env)->NewGlobalRef (env, data));
}

static void
gst_ahc_src_on_error (int error, gpointer user_data)
{
  GstAHCSrc *self = GST_AHC_SRC (user_data);

  GST_WARNING_OBJECT (self, "Received error code : %d", error);

}

static gboolean
gst_ahc_src_open (GstAHCSrc * self)
{
  GST_WARNING_OBJECT (self, "Openning camera");

  self->camera = gst_ah_camera_open (0);

  if (self->camera) {
    GstAHCParameters *params;
    JNIEnv *env = gst_dvm_get_env ();

    params = gst_ah_camera_get_parameters (self->camera);

    GST_WARNING_OBJECT (self, "Opened camera");
    if (params) {
      GstAHCSize *size;

      GST_WARNING_OBJECT (self, "Params : %s",
          gst_ahc_parameters_flatten (params));
      gst_ahc_parameters_set_preview_size (params, 1280, 720);
      gst_ahc_parameters_set_preview_format (params, ImageFormat_YV12);

      GST_WARNING_OBJECT (self, "Setting new params (%d) : %s",
          gst_ah_camera_set_parameters (self->camera, params),
          gst_ahc_parameters_flatten (params));
      size = gst_ahc_parameters_get_preview_size (params);
      self->caps = gst_caps_new_simple ("video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('Y', 'V', '1', '2'),
          "width", G_TYPE_INT, size->width,
          "height", G_TYPE_INT, size->height,
          "framerate", GST_TYPE_FRACTION, 30, 1, NULL);
      self->buffer_size = size->width * size->height *
          gst_ag_imageformat_get_bits_per_pixel
          (gst_ahc_parameters_get_preview_format (params)) / 8;
      gst_ahc_size_free (size);
      gst_ah_camera_set_preview_texture (self->camera, self->texture);
      gst_ah_camera_set_error_callback (self->camera, gst_ahc_src_on_error,
          self);
      gst_ah_camera_set_preview_callback_with_buffer (self->camera,
          gst_ahc_src_on_preview_frame, self);
      gst_ah_camera_add_callback_buffer (self->camera,
          (*env)->NewByteArray (env, self->buffer_size));
      gst_ah_camera_add_callback_buffer (self->camera,
          (*env)->NewByteArray (env, self->buffer_size));
      gst_ah_camera_add_callback_buffer (self->camera,
          (*env)->NewByteArray (env, self->buffer_size));

      {
        GList *list, *i;

        list = gst_ahc_parameters_get_supported_preview_formats (params);
        GST_WARNING_OBJECT (self, "Supported preview formats:");
        for (i = list; i; i = i->next) {
          int f = GPOINTER_TO_INT (i->data);

          GST_WARNING_OBJECT (self, "    %d", f);
        }
        gst_ahc_parameters_supported_preview_formats_free (list);

        list = gst_ahc_parameters_get_supported_preview_sizes (params);
        GST_WARNING_OBJECT (self, "Supported preview sizes:");
        for (i = list; i; i = i->next) {
          GstAHCSize *s = i->data;

          GST_WARNING_OBJECT (self, "    %dx%d", s->width, s->height);
        }
        gst_ahc_parameters_supported_preview_sizes_free (list);

        list = gst_ahc_parameters_get_supported_preview_fps_range (params);
        GST_WARNING_OBJECT (self, "Supported preview fps range:");
        for (i = list; i; i = i->next) {
          int *range = i->data;

          GST_WARNING_OBJECT (self, "    [%d, %d]", range[0], range[1]);
        }
        gst_ahc_parameters_supported_preview_fps_range_free (list);
      }
      gst_ahc_parameters_free (params);
    }

  } else {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Unable to open device '%d'.", 0), GST_ERROR_SYSTEM);
  }

  return (self->camera != NULL);
}

static GstStateChangeReturn
gst_ahc_src_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstAHCSrc *self = GST_AHC_SRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
      gint num_cams = gst_ah_camera_get_number_of_cameras ();
      gint i;

      GST_WARNING_OBJECT (self, "Found %d cameras on the system", num_cams);

      for (i = 0; i < num_cams; i++) {
        GstAHCCameraInfo info;
        if (gst_ah_camera_get_camera_info (i, &info)) {
          GST_WARNING_OBJECT (self, "Camera info for %d", i);
          GST_WARNING_OBJECT (self, "    Facing: %s (%d)",
              info.facing == CameraInfo_CAMERA_FACING_BACK ? "Back" : "Front",
              info.facing);
          GST_WARNING_OBJECT (self, "    Orientation: %d degrees",
              info.orientation);
        } else {
          GST_WARNING_OBJECT (self, "Error getting camera info for %d", i);
        }
      }

      if (num_cams > 0) {
        if (!gst_ahc_src_open (self))
          return GST_STATE_CHANGE_FAILURE;
      } else {
        GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
            ("There are no cameras available on this device."),
            GST_ERROR_SYSTEM);
        return GST_STATE_CHANGE_FAILURE;
      }
    }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (self->camera)
        gst_ah_camera_release (self->camera);
      self->camera = NULL;
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_ahc_src_start (GstBaseSrc * bsrc)
{
  GstAHCSrc *self = GST_AHC_SRC (bsrc);

  GST_WARNING_OBJECT (self, "Starting preview");
  if (self->camera) {
    gboolean ret = gst_ah_camera_start_preview (self->camera);
    if (ret)
      gst_ah_camera_set_preview_callback_with_buffer (self->camera,
          gst_ahc_src_on_preview_frame, self);
    return ret;
  } else {
    return FALSE;
  }
}

static gboolean
gst_ahc_src_stop (GstBaseSrc * bsrc)
{
  GstAHCSrc *self = GST_AHC_SRC (bsrc);

  GST_WARNING_OBJECT (self, "Stopping preview");
  if (self->camera)
    return gst_ah_camera_stop_preview (self->camera);
  else
    return TRUE;
}

static gboolean
gst_ahc_src_unlock (GstBaseSrc * bsrc)
{
  GstAHCSrc *self = GST_AHC_SRC (bsrc);

  GST_WARNING_OBJECT (self, "Unlocking create");
  self->flushing = TRUE;

  return TRUE;
}

static gboolean
gst_ahc_src_unlock_stop (GstBaseSrc * bsrc)
{
  GstAHCSrc *self = GST_AHC_SRC (bsrc);

  GST_WARNING_OBJECT (self, "Stopping unlock");
  self->flushing = FALSE;

  return TRUE;
}

typedef struct {
  GstAHCSrc *self;
  jbyteArray array;
  jbyte *data;
} FreeFuncBuffer;

static void
gst_ahc_src_buffer_free_func (gpointer priv)
{
  FreeFuncBuffer *data = (FreeFuncBuffer *) priv;
  GstAHCSrc *self = data->self;
  JNIEnv *env = gst_dvm_get_env ();

  (*env)->ReleaseByteArrayElements(env, data->array, data->data, JNI_ABORT);
  if (self->camera)
    gst_ah_camera_add_callback_buffer (self->camera, data->array);
  (*env)->DeleteGlobalRef (env, data->array);

  g_slice_free (FreeFuncBuffer, data);
}

static GstFlowReturn
gst_ahc_src_create (GstPushSrc * src, GstBuffer ** buffer)
{
  GstAHCSrc *self = GST_AHC_SRC (src);
  JNIEnv *env = gst_dvm_get_env ();
  FreeFuncBuffer *user_data;
  jbyteArray data = NULL;

  while (data == NULL) {
    data = g_async_queue_timeout_pop (self->queue, 100000);
    if (data == NULL && self->flushing)
      return GST_FLOW_WRONG_STATE;
  }

  //GST_WARNING_OBJECT (self, "Received data buffer %p", data);

  user_data = g_slice_new0 (FreeFuncBuffer);
  user_data->self = self;
  user_data->array = data;
  user_data->data = (*env)->GetByteArrayElements (env, data, NULL);

  *buffer = gst_buffer_new ();
  GST_BUFFER_DATA (*buffer) = (guint8 *) user_data->data;
  GST_BUFFER_SIZE (*buffer) = self->buffer_size;
  GST_BUFFER_MALLOCDATA (*buffer) = (gpointer) user_data;
  GST_BUFFER_FREE_FUNC (*buffer) = gst_ahc_src_buffer_free_func;
  gst_buffer_set_caps (*buffer, self->caps);

  return GST_FLOW_OK;
}
