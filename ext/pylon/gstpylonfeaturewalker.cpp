/* Copyright (C) 2022 Basler AG
 *
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

#include "gstpylonfeaturewalker.h"

#include "gstpylonintrospection.h"

#include <queue>
#include <unordered_set>

/* prototypes */
static std::vector<GParamSpec*> gst_pylon_camera_handle_node(
    GenApi::INode* node, GenApi::INodeMap& nodemap,
    const gchar* device_fullname);
static void gst_pylon_camera_install_specs(
    const std::vector<GParamSpec*>& specs_list, GObjectClass* oclass,
    gint& nprop);

static std::unordered_set<std::string> propfilter_set = {
    "Width",
    "Height",
    "PixelFormat",
    "AcquisitionFrameRateEnable",
    "AcquisitionFrameRate",
    "AcquisitionFrameRateAbs"};

GenApi::INode* gst_pylon_process_selector_features(
    GenApi::INode* node, std::vector<std::string>& enum_values) {
  g_return_val_if_fail(node, NULL);

  GenApi::INode* selector_node = NULL;
  std::string error_msg;

  auto sel_node = dynamic_cast<GenApi::ISelector*>(node);
  if (!sel_node) {
    std::string msg = std::string(node->GetName()) + " is an invalid node";
    throw Pylon::GenericException(msg.c_str(), __FILE__, __LINE__);
  }

  /* If the feature has no selectors then it is a "direct" feature, it does not
   * depend on any other selector */
  GenApi::FeatureList_t selectors;
  sel_node->GetSelectingFeatures(selectors);
  if (selectors.empty()) {
    enum_values.push_back("direct-feature");
    return selector_node;
  }

  /* At the time being features with multiple selectors are not supported */
  guint max_selectors = 1;
  if (selectors.size() > max_selectors) {
    error_msg = "\"" + std::string(node->GetDisplayName()) + "\"" +
                " has more than " + std::to_string(max_selectors) +
                " selectors, ignoring!";
    throw Pylon::GenericException(error_msg.c_str(), __FILE__, __LINE__);
  }

  /* At the time being only features with enum selectors are supported */
  auto selector = selectors.at(0);
  auto enum_node = dynamic_cast<GenApi::IEnumeration*>(selector);
  if (!enum_node) {
    error_msg = "\"" + std::string(node->GetDisplayName()) + "\"" +
                " is not an enumerator selector, ignoring!";
    throw Pylon::GenericException(error_msg.c_str(), __FILE__, __LINE__);
  }

  /* Add selector enum values */
  GenApi::NodeList_t enum_entries;
  /* Calculate prefix length to strip */
  const auto prefix_str = std::string("EnumEntry_") +
                          enum_node->GetNode()->GetName().c_str() +
                          std::string("_");
  auto prefix_len = prefix_str.length();
  enum_node->GetEntries(enum_entries);
  for (auto const& e : enum_entries) {
    auto enum_name = std::string(e->GetName());
    enum_values.push_back(enum_name.substr(prefix_len));
  }

  /* If the number of selector values (stored in enum_values) is 1, leave
   * selector_node NULL, hence treating the feature as a "direct" one. */
  if (1 < enum_values.size()) {
    selector_node = selector->GetNode();
  }

  return selector_node;
}

static std::vector<GParamSpec*> gst_pylon_camera_handle_node(
    GenApi::INode* node, GenApi::INodeMap& nodemap,
    const gchar* device_fullname) {
  GenApi::INode* selector_node = NULL;
  guint64 selector_value = 0;
  std::vector<GParamSpec*> specs_list;

  g_return_val_if_fail(node, specs_list);

  std::vector<std::string> enum_values;
  selector_node = gst_pylon_process_selector_features(node, enum_values);

  Pylon::CEnumParameter param;
  if (selector_node) {
    param.Attach(selector_node);
  }

  for (auto const& sel_pair : enum_values) {
    if (param.IsValid()) {
      selector_value = param.GetEntryByName(sel_pair.c_str())->GetValue();
    }
    specs_list.push_back(GstPylonParamFactory::make_param(
        nodemap, node, selector_node, selector_value, device_fullname));
  }

  return specs_list;
}

static void gst_pylon_camera_install_specs(
    const std::vector<GParamSpec*>& specs_list, GObjectClass* oclass,
    gint& nprop) {
  g_return_if_fail(oclass);

  if (!specs_list.empty()) {
    for (const auto& pspec : specs_list) {
      g_object_class_install_property(oclass, nprop, pspec);
      nprop++;
    }
  } else {
    throw Pylon::GenericException(
        "Could not install GParamSpecs, no GParamSpecs were created", __FILE__,
        __LINE__);
  }
}

void GstPylonFeatureWalker::install_properties(GObjectClass* oclass,
                                               GenApi::INodeMap& nodemap,
                                               const gchar* device_fullname) {
  g_return_if_fail(oclass);

  gint nprop = 1;
  GenApi::INode* root_node = nodemap.GetNode("Root");
  auto worklist = std::queue<GenApi::INode*>();
  Pylon::CStringParameter model_name(nodemap, "DeviceModelName");

  worklist.push(root_node);

  while (!worklist.empty()) {
    auto node = worklist.front();
    worklist.pop();

    /* Only handle real features that are not in the filter set, are not
     * selectors and are available */
    auto sel_node = dynamic_cast<GenApi::ISelector*>(node);
    if (node->IsFeature() && (node->GetVisibility() != GenApi::Invisible) &&
        sel_node && GenApi::IsAvailable(node) && !sel_node->IsSelector() &&
        propfilter_set.find(std::string(node->GetName())) ==
            propfilter_set.end()) {
      GenICam::gcstring value;
      GenICam::gcstring attrib;

      try {
        std::vector<GParamSpec*> specs_list =
            gst_pylon_camera_handle_node(node, nodemap, device_fullname);
        gst_pylon_camera_install_specs(specs_list, oclass, nprop);
      } catch (const Pylon::GenericException& e) {
        GST_FIXME("Unable to install property \"%s\" on device \"%s\": %s",
                  node->GetDisplayName().c_str(), model_name.GetValue().c_str(),
                  e.GetDescription());
      }
    }

    /* Walk down all categories */
    auto category_node = dynamic_cast<GenApi::ICategory*>(node);
    if (category_node) {
      GenApi::FeatureList_t features;
      category_node->GetFeatures(features);
      for (auto const& f : features) {
        worklist.push(f->GetNode());
      }
    }
  }
}
