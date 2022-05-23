/* Copyright (C) 2022 Basler AG
 *
 * Created by RidgeRun, LLC <support@ridgerun.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *     1. Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *     2. Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *     3. Neither the name of the copyright holder nor the names of
 *        its contributors may be used to endorse or promote products
 *        derived from this software without specific prior written
 *        permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * SECTION:element-gstpylonsrc
 *
 * The pylonsrc element captures images from Basler cameras.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v pylonsrc ! videoconvert ! autovideosink
 * ]|
 * Capture images from a Basler camera and display them.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstpylonsrc.h"

#include "gstpylon.h"

#include <gst/video/video.h>

GST_DEBUG_CATEGORY_STATIC (gst_pylon_src_debug_category);
#define GST_CAT_DEFAULT gst_pylon_src_debug_category

struct _GstPylonSrc
{
  GstPushSrc base_pylonsrc;
  GstPylon *pylon;
  guint64 offset;
  GstClockTime duration;
  GstVideoInfo video_info;

  gchar *device_name;
};

/* prototypes */


static void gst_pylon_src_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_pylon_src_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_pylon_src_finalize (GObject * object);

static GstCaps *gst_pylon_src_get_caps (GstBaseSrc * src, GstCaps * filter);
static GstCaps *gst_pylon_src_fixate (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_pylon_src_set_caps (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_pylon_src_decide_allocation (GstBaseSrc * src,
    GstQuery * query);
static gboolean gst_pylon_src_start (GstBaseSrc * src);
static gboolean gst_pylon_src_stop (GstBaseSrc * src);
static gboolean gst_pylon_src_unlock (GstBaseSrc * src);
static gboolean gst_pylon_src_unlock_stop (GstBaseSrc * src);
static gboolean gst_pylon_src_query (GstBaseSrc * src, GstQuery * query);
static void gst_plyon_src_add_metadata (GstPylonSrc * self, GstBuffer * buf);
static GstFlowReturn gst_pylon_src_create (GstPushSrc * src, GstBuffer ** buf);

enum
{
  PROP_0,
  PROP_DEVICE_NAME
};

/* pad templates */

static GstStaticPadTemplate gst_pylon_src_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (" {GRAY8, RGB, BGR} "))
    );


/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstPylonSrc, gst_pylon_src, GST_TYPE_PUSH_SRC,
    GST_DEBUG_CATEGORY_INIT (gst_pylon_src_debug_category, "pylonsrc", 0,
        "debug category for pylonsrc element"));

static void
gst_pylon_src_class_init (GstPylonSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSrcClass *base_src_class = GST_BASE_SRC_CLASS (klass);
  GstPushSrcClass *push_src_class = GST_PUSH_SRC_CLASS (klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &gst_pylon_src_src_template);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Basler/Pylon source element", "Source/Video/Hardware",
      "Source element for Basler cameras",
      "Michael Gruner <michael.gruner@ridgerun.com>");

  gobject_class->set_property = gst_pylon_src_set_property;
  gobject_class->get_property = gst_pylon_src_get_property;
  gobject_class->finalize = gst_pylon_src_finalize;

  g_object_class_install_property (gobject_class, PROP_DEVICE_NAME,
      g_param_spec_string ("device-name", "Device name",
          "The name of the device to use", NULL, G_PARAM_READWRITE));

  base_src_class->get_caps = GST_DEBUG_FUNCPTR (gst_pylon_src_get_caps);
  base_src_class->fixate = GST_DEBUG_FUNCPTR (gst_pylon_src_fixate);
  base_src_class->set_caps = GST_DEBUG_FUNCPTR (gst_pylon_src_set_caps);
  base_src_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_pylon_src_decide_allocation);
  base_src_class->start = GST_DEBUG_FUNCPTR (gst_pylon_src_start);
  base_src_class->stop = GST_DEBUG_FUNCPTR (gst_pylon_src_stop);
  base_src_class->unlock = GST_DEBUG_FUNCPTR (gst_pylon_src_unlock);
  base_src_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_pylon_src_unlock_stop);
  base_src_class->query = GST_DEBUG_FUNCPTR (gst_pylon_src_query);

  push_src_class->create = GST_DEBUG_FUNCPTR (gst_pylon_src_create);

  gst_pylon_initialize ();
}

static void
gst_pylon_src_init (GstPylonSrc * self)
{
  self->pylon = NULL;
  self->offset = G_GUINT64_CONSTANT (0);
  self->duration = GST_CLOCK_TIME_NONE;
  gst_video_info_init (&self->video_info);
}

static void
gst_pylon_src_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPylonSrc *self = GST_PYLON_SRC (object);

  GST_LOG_OBJECT (self, "set_property");

  GST_OBJECT_LOCK (self);

  switch (property_id) {
    case PROP_DEVICE_NAME:
      g_free (self->device_name);
      self->device_name = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (self);
}

static void
gst_pylon_src_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstPylonSrc *self = GST_PYLON_SRC (object);

  GST_LOG_OBJECT (self, "get_property");

  GST_OBJECT_LOCK (self);

  switch (property_id) {
    case PROP_DEVICE_NAME:
      g_value_set_string (value, self->device_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (self);
}

static void
gst_pylon_src_finalize (GObject * object)
{
  GstPylonSrc *self = GST_PYLON_SRC (object);

  GST_LOG_OBJECT (self, "finalize");

  /* clean up object here */

  G_OBJECT_CLASS (gst_pylon_src_parent_class)->finalize (object);
}

/* get caps from subclass */
static GstCaps *
gst_pylon_src_get_caps (GstBaseSrc * src, GstCaps * filter)
{
  GstPylonSrc *self = GST_PYLON_SRC (src);
  GstCaps *outcaps = NULL;
  GError *error = NULL;

  if (!self->pylon) {
    outcaps = gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (self));
    GST_INFO_OBJECT (self,
        "Camera not open yet, returning src template caps %" GST_PTR_FORMAT,
        outcaps);
    goto out;
  }

  outcaps = gst_pylon_query_configuration (self->pylon, &error);

  if (outcaps == NULL && error) {
    goto log_gst_error;
  }

  GST_DEBUG_OBJECT (self, "Camera returned caps %" GST_PTR_FORMAT, outcaps);

  if (filter) {
    GstCaps *tmp = outcaps;

    GST_DEBUG_OBJECT (self, "Filtering with %" GST_PTR_FORMAT, filter);

    outcaps = gst_caps_intersect (outcaps, filter);
    gst_caps_unref (tmp);
  }

  GST_INFO_OBJECT (self, "Returning caps %" GST_PTR_FORMAT, outcaps);

  goto out;

log_gst_error:
  GST_ELEMENT_ERROR (self, LIBRARY, FAILED,
      ("Failed to get caps."), ("%s", error->message));
  g_error_free (error);

out:
  return outcaps;
}

/* called if, in negotiation, caps need fixating */
static GstCaps *
gst_pylon_src_fixate (GstBaseSrc * src, GstCaps * caps)
{
  GstPylonSrc *self = GST_PYLON_SRC (src);
  GstCaps *outcaps = NULL;
  GstStructure *st = NULL;
  static const gint preferred_width = 1920;
  static const gint preferred_height = 1080;
  static const gint preferred_framerate_num = 30;
  static const gint preferred_framerate_den = 1;


  GST_DEBUG_OBJECT (self, "Fixating caps %" GST_PTR_FORMAT, caps);

  if (gst_caps_is_fixed (caps)) {
    GST_DEBUG_OBJECT (self, "Caps are already fixed");
    return caps;
  }

  outcaps = gst_caps_new_empty ();
  st = gst_structure_copy (gst_caps_get_structure (caps, 0));
  gst_caps_unref (caps);

  gst_structure_fixate_field_nearest_int (st, "width", preferred_width);
  gst_structure_fixate_field_nearest_int (st, "height", preferred_height);
  gst_structure_fixate_field_nearest_fraction (st, "framerate",
      preferred_framerate_num, preferred_framerate_den);

  gst_caps_append_structure (outcaps, st);

  /* Fixate the remainder of the fields */
  outcaps = gst_caps_fixate (outcaps);

  GST_INFO_OBJECT (self, "Fixated caps to %" GST_PTR_FORMAT, outcaps);

  return outcaps;
}

/* notify the subclass of new caps */
static gboolean
gst_pylon_src_set_caps (GstBaseSrc * src, GstCaps * caps)
{
  GstPylonSrc *self = GST_PYLON_SRC (src);
  GstStructure *st = NULL;
  gint numerator = 0;
  gint denominator = 0;
  GError *error = NULL;
  gboolean ret = FALSE;
  const gchar *action = NULL;

  GST_INFO_OBJECT (self, "Setting new caps: %" GST_PTR_FORMAT, caps);

  st = gst_caps_get_structure (caps, 0);
  gst_structure_get_fraction (st, "framerate", &numerator, &denominator);

  GST_OBJECT_LOCK (self);
  self->duration = gst_util_uint64_scale (GST_SECOND, denominator, numerator);
  GST_OBJECT_UNLOCK (self);

  ret = gst_pylon_stop (self->pylon, &error);
  if (FALSE == ret && error) {
    action = "stop";
    goto error;
  }

  ret = gst_pylon_set_configuration (self->pylon, caps, &error);
  if (FALSE == ret && error) {
    action = "configure";
    goto error;
  }

  ret = gst_pylon_start (self->pylon, &error);
  if (FALSE == ret && error) {
    action = "start";
    goto error;
  }

  ret = gst_video_info_from_caps (&self->video_info, caps);

  goto out;

error:
  GST_ELEMENT_ERROR (self, LIBRARY, FAILED,
      ("Failed to %s camera.", action), ("%s", error->message));
  g_error_free (error);

out:
  return ret;
}

/* setup allocation query */
static gboolean
gst_pylon_src_decide_allocation (GstBaseSrc * src, GstQuery * query)
{
  GstPylonSrc *self = GST_PYLON_SRC (src);

  GST_LOG_OBJECT (self, "decide_allocation");

  return TRUE;
}

/* start and stop processing, ideal for opening/closing the resource */
static gboolean
gst_pylon_src_start (GstBaseSrc * src)
{
  GstPylonSrc *self = GST_PYLON_SRC (src);
  GError *error = NULL;
  gboolean ret = TRUE;

  GST_INFO_OBJECT (self, "Starting camera device");

  self->pylon = gst_pylon_new (self->device_name, &error);

  if (error) {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED,
        ("Failed to start camera."), ("%s", error->message));
    g_error_free (error);
    ret = FALSE;

  } else {
    self->offset = 0;
  }

  return ret;
}

static gboolean
gst_pylon_src_stop (GstBaseSrc * src)
{
  GstPylonSrc *self = GST_PYLON_SRC (src);
  GError *error = NULL;
  gboolean ret = TRUE;

  GST_INFO_OBJECT (self, "Stopping camera device");

  ret = gst_pylon_stop (self->pylon, &error);

  if (ret == FALSE && error) {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED,
        ("Failed to close camera."), ("%s", error->message));
    g_error_free (error);
  }

  gst_pylon_free (self->pylon);
  self->pylon = NULL;

  return ret;
}

/* unlock any pending access to the resource. subclasses should unlock
 * any function ASAP. */
static gboolean
gst_pylon_src_unlock (GstBaseSrc * src)
{
  GstPylonSrc *self = GST_PYLON_SRC (src);

  GST_LOG_OBJECT (self, "unlock");

  return TRUE;
}

/* Clear any pending unlock request, as we succeeded in unlocking */
static gboolean
gst_pylon_src_unlock_stop (GstBaseSrc * src)
{
  GstPylonSrc *self = GST_PYLON_SRC (src);

  GST_LOG_OBJECT (self, "unlock_stop");

  return TRUE;
}

/* notify subclasses of a query */
static gboolean
gst_pylon_src_query (GstBaseSrc * src, GstQuery * query)
{
  GstPylonSrc *self = GST_PYLON_SRC (src);

  GST_LOG_OBJECT (self, "query");

  return GST_BASE_SRC_CLASS (gst_pylon_src_parent_class)->query (src, query);
}

/* add time metadata to buffer */
static void
gst_plyon_src_add_metadata (GstPylonSrc * self, GstBuffer * buf)
{
  GstClock *clock = NULL;
  GstClockTime abs_time = GST_CLOCK_TIME_NONE;
  GstClockTime base_time = GST_CLOCK_TIME_NONE;
  GstClockTime timestamp = GST_CLOCK_TIME_NONE;
  GstVideoFormat format = GST_VIDEO_FORMAT_UNKNOWN;
  guint width = 0;
  guint height = 0;
  guint n_planes = 0;
  gint stride[GST_VIDEO_MAX_PLANES] = { 0 };

  g_return_if_fail (self);
  g_return_if_fail (buf);

  GST_LOG_OBJECT (self, "Setting buffer metadata");

  GST_OBJECT_LOCK (self);
  /* set duration */
  GST_BUFFER_DURATION (buf) = self->duration;

  if ((clock = GST_ELEMENT_CLOCK (self))) {
    /* we have a clock, get base time and ref clock */
    base_time = GST_ELEMENT (self)->base_time;
    gst_object_ref (clock);
  } else {
    /* no clock, can't set timestamps */
    base_time = GST_CLOCK_TIME_NONE;
  }
  GST_OBJECT_UNLOCK (self);

  /* sample pipeline clock */
  if (clock) {
    abs_time = gst_clock_get_time (clock);
    gst_object_unref (clock);
  } else {
    abs_time = GST_CLOCK_TIME_NONE;
  }

  timestamp = abs_time - base_time;

  GST_BUFFER_TIMESTAMP (buf) = timestamp;
  GST_BUFFER_OFFSET (buf) = self->offset;
  GST_BUFFER_OFFSET_END (buf) = self->offset + 1;

  /* add video meta data */
  format = GST_VIDEO_INFO_FORMAT (&self->video_info);
  width = GST_VIDEO_INFO_WIDTH (&self->video_info);
  height = GST_VIDEO_INFO_HEIGHT (&self->video_info);
  n_planes = GST_VIDEO_INFO_N_PLANES (&self->video_info);

  /* stride is being constructed manually since the pylon stride
   * does not match with the GstVideoInfo obtained stride. */
  for (gint p = 0; p < n_planes; p++) {
    stride[p] = width * GST_VIDEO_INFO_COMP_PSTRIDE (&self->video_info, p);
  }

  gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE, format, width,
      height, n_planes, self->video_info.offset, stride);
}

/* ask the subclass to create a buffer with offset and size, the default
 * implementation will call alloc and fill. */
static GstFlowReturn
gst_pylon_src_create (GstPushSrc * src, GstBuffer ** buf)
{
  GstPylonSrc *self = GST_PYLON_SRC (src);
  GError *error = NULL;
  gboolean pylon_ret = TRUE;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_LOG_OBJECT (self, "Creating buffer");

  pylon_ret = gst_pylon_capture (self->pylon, buf, &error);

  if (pylon_ret == FALSE) {
    if (error) {
      GST_ELEMENT_ERROR (self, LIBRARY, FAILED,
          ("Failed to create buffer."), ("%s", error->message));
      g_error_free (error);
    }
    ret = GST_FLOW_ERROR;
  }

  gst_plyon_src_add_metadata (self, *buf);
  self->offset++;

  return ret;
}
