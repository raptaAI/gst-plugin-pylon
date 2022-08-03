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

#include "gstpylonstreamgrabber.h"

#include "gstpylonfeaturewalker.h"
#include "gstpylonintrospection.h"
#include "gstpylonparamspecs.h"

typedef struct _GstPylonStreamGrabberPrivate GstPylonStreamGrabberPrivate;
struct _GstPylonStreamGrabberPrivate {
  std::shared_ptr<Pylon::CBaslerUniversalInstantCamera> camera;
};

/************************************************************
 * Start of GObject definition
 ***********************************************************/

static gchar* gst_pylon_stream_grabber_get_sanitized_name(
    const Pylon::CBaslerUniversalInstantCamera& camera) {
  Pylon::String_t cam_name = camera.GetDeviceInfo().GetFullName();

  /* Convert camera name to a valid string */
  return gst_pylon_param_spec_sanitize_name(cam_name.c_str());
}

static void gst_pylon_stream_grabber_init(GstPylonStreamGrabber* self);
static void gst_pylon_stream_grabber_class_init(
    GstPylonStreamGrabberClass* klass,
    Pylon::CBaslerUniversalInstantCamera* camera);
static gpointer gst_pylon_stream_grabber_parent_class = NULL;
static gint GstPylonStreamGrabber_private_offset;
static void gst_pylon_stream_grabber_class_intern_init(
    gpointer klass, Pylon::CBaslerUniversalInstantCamera* camera) {
  gst_pylon_stream_grabber_parent_class = g_type_class_peek_parent(klass);
  if (GstPylonStreamGrabber_private_offset != 0)
    g_type_class_adjust_private_offset(klass,
                                       &GstPylonStreamGrabber_private_offset);
  gst_pylon_stream_grabber_class_init((GstPylonStreamGrabberClass*)klass,
                                      camera);
}
static inline gpointer gst_pylon_stream_grabber_get_instance_private(
    GstPylonStreamGrabber* self) {
  return (G_STRUCT_MEMBER_P(self, GstPylonStreamGrabber_private_offset));
}

GType gst_pylon_stream_grabber_register(
    const Pylon::CBaslerUniversalInstantCamera& exemplar) {
  GTypeInfo typeinfo = {
      sizeof(GstPylonStreamGrabberClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_pylon_stream_grabber_class_intern_init,
      NULL,
      &exemplar,
      sizeof(GstPylonStreamGrabber),
      0,
      (GInstanceInitFunc)gst_pylon_stream_grabber_init,
  };

  gchar* type_name = gst_pylon_stream_grabber_get_sanitized_name(exemplar);

  GType type = g_type_from_name(type_name);
  if (!type) {
    type = g_type_register_static(G_TYPE_OBJECT, type_name, &typeinfo,
                                  static_cast<GTypeFlags>(0));
  }

  g_free(type_name);

  GstPylonStreamGrabber_private_offset =
      g_type_add_instance_private(type, sizeof(GstPylonStreamGrabberPrivate));

  return type;
}

/************************************************************
 * End of GObject definition
 ***********************************************************/

/* prototypes */
static void gst_pylon_stream_grabber_install_properties(
    GstPylonStreamGrabberClass* klass,
    Pylon::CBaslerUniversalInstantCamera* camera);
template <typename F, typename P>
static void gst_pylon_stream_grabber_set_pylon_property(
    GenApi::INodeMap& nodemap, F get_value, const GValue* value,
    const gchar* name);
static void gst_pylon_stream_grabber_set_enum_property(
    GenApi::INodeMap& nodemap, const GValue* value, const gchar* name);
template <typename F, typename P>
static void gst_pylon_stream_grabber_set_pylon_selector(
    GenApi::INodeMap& nodemap, F get_value, const GValue* value,
    const gchar* feature, const gchar* selector, guint64& selector_value);
static void gst_pylon_stream_grabber_set_enum_selector(
    GenApi::INodeMap& nodemap, const GValue* value, const gchar* feature_name,
    const gchar* selector_name, guint64& selector_value);
template <typename T, typename P>
static T gst_pylon_stream_grabber_get_pylon_property(GenApi::INodeMap& nodemap,
                                                     const gchar* name);
static gint gst_pylon_stream_grabber_get_enum_property(
    GenApi::INodeMap& nodemap, const gchar* name);
static void gst_pylon_stream_grabber_set_property(GObject* object,
                                                  guint property_id,
                                                  const GValue* value,
                                                  GParamSpec* pspec);
static void gst_pylon_stream_grabber_get_property(GObject* object,
                                                  guint property_id,
                                                  GValue* value,
                                                  GParamSpec* pspec);
static void gst_pylon_stream_grabber_finalize(GObject* self);

static void gst_pylon_stream_grabber_install_properties(
    GstPylonStreamGrabberClass* klass,
    Pylon::CBaslerUniversalInstantCamera* camera) {
  g_return_if_fail(klass);
  g_return_if_fail(camera);

  GenApi::INodeMap& nodemap = camera->GetNodeMap();
  GObjectClass* oclass = G_OBJECT_CLASS(klass);

  GstPylonFeatureWalker::install_properties(oclass, nodemap);
}

static void gst_pylon_stream_grabber_class_init(
    GstPylonStreamGrabberClass* klass,
    Pylon::CBaslerUniversalInstantCamera* exemplar) {
  GObjectClass* oclass = G_OBJECT_CLASS(klass);

  oclass->set_property = gst_pylon_stream_grabber_set_property;
  oclass->get_property = gst_pylon_stream_grabber_get_property;
  oclass->finalize = gst_pylon_stream_grabber_finalize;

  gst_pylon_stream_grabber_install_properties(klass, exemplar);
}

static void gst_pylon_stream_grabber_init(GstPylonStreamGrabber* self) {}

template <typename F, typename P>
static void gst_pylon_stream_grabber_set_pylon_property(
    GenApi::INodeMap& nodemap, F get_value, const GValue* value,
    const gchar* name) {
  P param(nodemap, name);
  param.SetValue(get_value(value));
}

static void gst_pylon_stream_grabber_set_enum_property(
    GenApi::INodeMap& nodemap, const GValue* value, const gchar* name) {
  Pylon::CEnumParameter param(nodemap, name);
  param.SetIntValue(g_value_get_enum(value));
}

template <typename F, typename P>
static void gst_pylon_stream_grabber_set_pylon_selector(
    GenApi::INodeMap& nodemap, F get_value, const GValue* value,
    const gchar* feature_name, const gchar* selector_name,
    guint64& selector_value) {
  Pylon::CEnumParameter selparam(nodemap, selector_name);
  selparam.SetIntValue(selector_value);

  gst_pylon_stream_grabber_set_pylon_property<F, P>(nodemap, get_value, value,
                                                    feature_name);
}

static void gst_pylon_stream_grabber_set_enum_selector(
    GenApi::INodeMap& nodemap, const GValue* value, const gchar* feature_name,
    const gchar* selector_name, guint64& selector_value) {
  Pylon::CEnumParameter selparam(nodemap, selector_name);
  selparam.SetIntValue(selector_value);

  gst_pylon_stream_grabber_set_enum_property(nodemap, value, feature_name);
}

template <typename T, typename P>
static T gst_pylon_stream_grabber_get_pylon_property(GenApi::INodeMap& nodemap,
                                                     const gchar* name) {
  P param(nodemap, name);
  return param.GetValue();
}

static gint gst_pylon_stream_grabber_get_enum_property(
    GenApi::INodeMap& nodemap, const gchar* name) {
  Pylon::CEnumParameter param(nodemap, name);
  return param.GetIntValue();
}

static void gst_pylon_stream_grabber_set_property(GObject* object,
                                                  guint property_id,
                                                  const GValue* value,
                                                  GParamSpec* pspec) {
  GstPylonStreamGrabber* self = (GstPylonStreamGrabber*)object;
  GstPylonStreamGrabberPrivate* priv = (GstPylonStreamGrabberPrivate*)
      gst_pylon_stream_grabber_get_instance_private(self);
  GType value_type = g_type_fundamental(G_VALUE_TYPE(value));

  try {
    GenApi::INodeMap& nodemap = priv->camera->GetNodeMap();
    if (G_TYPE_INT64 == value_type) {
      /* The value accepted by the pspec is an INT64, it can be an int
       * feature or an int selector. */
      if (GST_PYLON_PARAM_FLAG_IS_SET(pspec, GST_PYLON_PARAM_IS_SELECTOR)) {
        GstPylonParamSpecSelectorInt64* lspec =
            GST_PYLON_PARAM_SPEC_SELECTOR_INT64(pspec);
        typedef gint64 (*GGetInt64)(const GValue*);
        gst_pylon_stream_grabber_set_pylon_selector<GGetInt64,
                                                    Pylon::CIntegerParameter>(
            nodemap, g_value_get_int64, value, lspec->feature, lspec->selector,
            lspec->selector_value);
      } else {
        typedef gint64 (*GGetInt64)(const GValue*);
        gst_pylon_stream_grabber_set_pylon_property<GGetInt64,
                                                    Pylon::CIntegerParameter>(
            nodemap, g_value_get_int64, value, pspec->name);
      }
    } else if (G_TYPE_BOOLEAN == value_type) {
      if (GST_PYLON_PARAM_FLAG_IS_SET(pspec, GST_PYLON_PARAM_IS_SELECTOR)) {
        GstPylonParamSpecSelectorBool* lspec =
            GST_PYLON_PARAM_SPEC_SELECTOR_BOOL(pspec);
        typedef gboolean (*GGetBool)(const GValue*);
        gst_pylon_stream_grabber_set_pylon_selector<GGetBool,
                                                    Pylon::CBooleanParameter>(
            nodemap, g_value_get_boolean, value, lspec->feature,
            lspec->selector, lspec->selector_value);
      } else {
        typedef gboolean (*GGetBool)(const GValue*);
        gst_pylon_stream_grabber_set_pylon_property<GGetBool,
                                                    Pylon::CBooleanParameter>(
            nodemap, g_value_get_boolean, value, pspec->name);
      }
    } else if (G_TYPE_FLOAT == value_type) {
      if (GST_PYLON_PARAM_FLAG_IS_SET(pspec, GST_PYLON_PARAM_IS_SELECTOR)) {
        GstPylonParamSpecSelectorFloat* lspec =
            GST_PYLON_PARAM_SPEC_SELECTOR_FLOAT(pspec);
        typedef gfloat (*GGetFloat)(const GValue*);
        gst_pylon_stream_grabber_set_pylon_selector<GGetFloat,
                                                    Pylon::CFloatParameter>(
            nodemap, g_value_get_float, value, lspec->feature, lspec->selector,
            lspec->selector_value);
      } else {
        typedef gfloat (*GGetFloat)(const GValue*);
        gst_pylon_stream_grabber_set_pylon_property<GGetFloat,
                                                    Pylon::CFloatParameter>(
            nodemap, g_value_get_float, value, pspec->name);
      }
    } else if (G_TYPE_STRING == value_type) {
      if (GST_PYLON_PARAM_FLAG_IS_SET(pspec, GST_PYLON_PARAM_IS_SELECTOR)) {
        GstPylonParamSpecSelectorStr* lspec =
            GST_PYLON_PARAM_SPEC_SELECTOR_STR(pspec);
        typedef const gchar* (*GGetString)(const GValue*);
        gst_pylon_stream_grabber_set_pylon_selector<GGetString,
                                                    Pylon::CStringParameter>(
            nodemap, g_value_get_string, value, lspec->feature, lspec->selector,
            lspec->selector_value);
      } else {
        typedef const gchar* (*GGetString)(const GValue*);
        gst_pylon_stream_grabber_set_pylon_property<GGetString,
                                                    Pylon::CStringParameter>(
            nodemap, g_value_get_string, value, pspec->name);
      }
    } else if (G_TYPE_ENUM == value_type) {
      if (GST_PYLON_PARAM_FLAG_IS_SET(pspec, GST_PYLON_PARAM_IS_SELECTOR)) {
        GstPylonParamSpecSelectorEnum* lspec =
            (GstPylonParamSpecSelectorEnum*)pspec;
        gst_pylon_stream_grabber_set_enum_selector(
            nodemap, value, lspec->feature, lspec->selector,
            lspec->selector_value);
      } else {
        gst_pylon_stream_grabber_set_enum_property(nodemap, value, pspec->name);
      }
    } else {
      g_warning("Unsupported GType: %s", g_type_name(pspec->value_type));
      std::string msg =
          "Unsupported GType: " + std::string(g_type_name(pspec->value_type));
      throw Pylon::GenericException(msg.c_str(), __FILE__, __LINE__);
    }
  } catch (const Pylon::GenericException& e) {
    GST_ERROR("Unable to set pylon property \"%s\" on \"%s\": %s", pspec->name,
              priv->camera->GetDeviceInfo().GetFriendlyName().c_str(),
              e.GetDescription());
  }
}

static void gst_pylon_stream_grabber_get_property(GObject* object,
                                                  guint property_id,
                                                  GValue* value,
                                                  GParamSpec* pspec) {
  GstPylonStreamGrabber* self = (GstPylonStreamGrabber*)object;
  GstPylonStreamGrabberPrivate* priv = (GstPylonStreamGrabberPrivate*)
      gst_pylon_stream_grabber_get_instance_private(self);

  try {
    GenApi::INodeMap& nodemap = priv->camera->GetNodeMap();
    switch (g_type_fundamental(pspec->value_type)) {
      case G_TYPE_INT64:
        g_value_set_int64(
            value, gst_pylon_stream_grabber_get_pylon_property<
                       gint64, Pylon::CIntegerParameter>(nodemap, pspec->name));
        break;
      case G_TYPE_BOOLEAN:
        g_value_set_boolean(value, gst_pylon_stream_grabber_get_pylon_property<
                                       gboolean, Pylon::CBooleanParameter>(
                                       nodemap, pspec->name));
        break;
      case G_TYPE_FLOAT:
        g_value_set_float(
            value, gst_pylon_stream_grabber_get_pylon_property<
                       gfloat, Pylon::CFloatParameter>(nodemap, pspec->name));
        break;
      case G_TYPE_STRING:
        g_value_set_string(
            value, gst_pylon_stream_grabber_get_pylon_property<
                       GenICam::gcstring, Pylon::CStringParameter>(nodemap,
                                                                   pspec->name)
                       .c_str());
        break;
      case G_TYPE_ENUM:
        g_value_set_enum(value, gst_pylon_stream_grabber_get_enum_property(
                                    nodemap, pspec->name));
        break;
      default:
        g_warning("Unsupported GType: %s", g_type_name(pspec->value_type));
        std::string msg =
            "Unsupported GType: " + std::string(g_type_name(pspec->value_type));
        throw Pylon::GenericException(msg.c_str(), __FILE__, __LINE__);
    }
  } catch (const Pylon::GenericException& e) {
    GST_ERROR("Unable to get pylon property \"%s\" on \"%s\": %s", pspec->name,
              priv->camera->GetDeviceInfo().GetFriendlyName().c_str(),
              e.GetDescription());
  }
}

GObject* gst_pylon_stream_grabber_new(
    std::shared_ptr<Pylon::CBaslerUniversalInstantCamera> camera) {
  gchar* type_name = gst_pylon_stream_grabber_get_sanitized_name(*camera);

  GType type = g_type_from_name(type_name);
  g_free(type_name);

  GObject* obj = G_OBJECT(g_object_new(type, NULL));
  GstPylonStreamGrabber* self = (GstPylonStreamGrabber*)obj;
  GstPylonStreamGrabberPrivate* priv = (GstPylonStreamGrabberPrivate*)
      gst_pylon_stream_grabber_get_instance_private(self);

  priv->camera = camera;

  return obj;
}

static void gst_pylon_stream_grabber_finalize(GObject* object) {
  GstPylonStreamGrabber* self = (GstPylonStreamGrabber*)object;
  GstPylonStreamGrabberPrivate* priv = (GstPylonStreamGrabberPrivate*)
      gst_pylon_stream_grabber_get_instance_private(self);

  priv->camera = NULL;

  G_OBJECT_CLASS(gst_pylon_stream_grabber_parent_class)->finalize(object);
}