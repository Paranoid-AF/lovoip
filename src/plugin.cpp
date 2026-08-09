// lovoip - vaudio_celt voice codec simulator (CLAP plugin)
//
// Simulates the Source Engine vaudio_celt sound with pure DSP:
// bandlimiting, naive resampling and per-frame band selection with
// envelope quantization. See dsp.cpp for the codec simulation.

#include <clap/clap.h>

#define _CRT_SECURE_NO_WARNINGS
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "lovoip/params.hpp"
#include "clap_plugin.hpp"

namespace lovoip {

namespace {

Plugin* fromClap(const clap_plugin_t* p) {
   return static_cast<Plugin*>(p->plugin_data);
}

// ---- clap_plugin_t trampolines -------------------------------------------

bool CLAP_ABI s_init(const clap_plugin_t* p) { return fromClap(p)->init(); }
void CLAP_ABI s_destroy(const clap_plugin_t* p) { fromClap(p)->destroy(); }
bool CLAP_ABI s_activate(const clap_plugin_t* p, double sr, std::uint32_t minF, std::uint32_t maxF) {
   return fromClap(p)->activate(sr, minF, maxF);
}
void CLAP_ABI s_deactivate(const clap_plugin_t* p) { fromClap(p)->deactivate(); }
bool CLAP_ABI s_start_processing(const clap_plugin_t* p) { return fromClap(p)->startProcessing(); }
void CLAP_ABI s_stop_processing(const clap_plugin_t* p) { fromClap(p)->stopProcessing(); }
void CLAP_ABI s_reset(const clap_plugin_t* p) { fromClap(p)->reset(); }
clap_process_status CLAP_ABI s_process(const clap_plugin_t* p, const clap_process_t* proc) {
   return fromClap(p)->process(proc);
}
const void* CLAP_ABI s_get_extension(const clap_plugin_t* p, const char* id) {
   return fromClap(p)->getExtension(id);
}
void CLAP_ABI s_on_main_thread(const clap_plugin_t* p) { fromClap(p)->onMainThread(); }

// ---- params extension -----------------------------------------------------

std::uint32_t CLAP_ABI p_count(const clap_plugin_t* p) { return fromClap(p)->paramsCount(); }
bool CLAP_ABI p_get_info(const clap_plugin_t* p, std::uint32_t i, clap_param_info_t* info) {
   return fromClap(p)->paramsInfo(i, info);
}
bool CLAP_ABI p_get_value(const clap_plugin_t* p, clap_id id, double* out) {
   return fromClap(p)->paramsValue(id, out);
}
bool CLAP_ABI p_value_to_text(const clap_plugin_t* p, clap_id id, double v, char* d, std::uint32_t n) {
   return fromClap(p)->paramsValueToText(id, v, d, n);
}
bool CLAP_ABI p_text_to_value(const clap_plugin_t* p, clap_id id, const char* d, double* out) {
   return fromClap(p)->paramsTextToValue(id, d, out);
}
void CLAP_ABI p_flush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t* out) {
   fromClap(p)->paramsFlush(in, out);
}

const clap_plugin_params_t kParamsExt = {
   p_count, p_get_info, p_get_value, p_value_to_text, p_text_to_value, p_flush,
};

// ---- state extension ------------------------------------------------------

bool CLAP_ABI st_save(const clap_plugin_t* p, const clap_ostream_t* s) { return fromClap(p)->stateSave(s); }
bool CLAP_ABI st_load(const clap_plugin_t* p, const clap_istream_t* s) { return fromClap(p)->stateLoad(s); }

const clap_plugin_state_t kStateExt = { st_save, st_load };

// ---- latency extension ----------------------------------------------------

std::uint32_t CLAP_ABI lat_get(const clap_plugin_t* p) { return fromClap(p)->latencyGet(); }

const clap_plugin_latency_t kLatencyExt = { lat_get };

// ---- audio-ports extension ------------------------------------------------

std::uint32_t CLAP_ABI ap_count(const clap_plugin_t* p, bool isInput) {
   return fromClap(p)->audioPortsCount(isInput);
}
bool CLAP_ABI ap_get(const clap_plugin_t* p, std::uint32_t i, bool isInput, clap_audio_port_info_t* info) {
   return fromClap(p)->audioPortsInfo(i, isInput, info);
}

const clap_plugin_audio_ports_t kAudioPortsExt = { ap_count, ap_get };

// ---- factory / entry ------------------------------------------------------

const clap_plugin_descriptor_t kDescriptor = {
   CLAP_VERSION_INIT,
   kPluginId,
   kPluginName,
   kPluginVendor,
   "https://github.com/john/lovoip",
   "",
   "",
   kPluginVersion,
   "Simulates the Source Engine vaudio_celt voice codec (bandlimit + low-bitrate CELT character).",
   nullptr, // features, set below
};

const char* const kFeatures[] = {
   CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
   "Voice",
   "Codec",
   "Lo-Fi",
   nullptr,
};

std::uint32_t CLAP_ABI f_count(const clap_plugin_factory_t*) { return 1; }

const clap_plugin_descriptor_t* CLAP_ABI f_get_descriptor(const clap_plugin_factory_t*, std::uint32_t index) {
   return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_t* CLAP_ABI f_create(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
   if (!clap_version_is_compatible(host->clap_version)) return nullptr;
   if (std::strcmp(id, kPluginId) != 0) return nullptr;
   auto* plugin = new Plugin(host);
   return plugin->clapPlugin();
}

const clap_plugin_factory_t kFactory = { f_count, f_get_descriptor, f_create };

bool CLAP_ABI entry_init(const char*) { return true; }
void CLAP_ABI entry_deinit() {}

const void* CLAP_ABI entry_get_factory(const char* factoryId) {
   if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0) return &kFactory;
   return nullptr;
}

const clap_plugin_entry_t kEntry = {
   CLAP_VERSION_INIT,
   entry_init,
   entry_deinit,
   entry_get_factory,
};

} // namespace

// ---- Plugin implementation ------------------------------------------------

Plugin::Plugin(const clap_host_t* host) : host_(host) {
   plugin_.desc = &kDescriptor;
   plugin_.plugin_data = this;
   plugin_.init = s_init;
   plugin_.destroy = s_destroy;
   plugin_.activate = s_activate;
   plugin_.deactivate = s_deactivate;
   plugin_.start_processing = s_start_processing;
   plugin_.stop_processing = s_stop_processing;
   plugin_.reset = s_reset;
   plugin_.process = s_process;
   plugin_.get_extension = s_get_extension;
   plugin_.on_main_thread = s_on_main_thread;
}

bool Plugin::init() {
   if (host_) {
      hostLog_ = static_cast<const clap_host_log_t*>(host_->get_extension(host_, CLAP_EXT_LOG));
   }
   return true;
}

void Plugin::destroy() { delete this; }

void Plugin::applyParams() {
   dsp_.setQuality(quality_);
}

bool Plugin::activate(double sampleRate, std::uint32_t, std::uint32_t maxFrames) {
   sampleRate_ = sampleRate;
   dsp_.prepare(sampleRate, maxFrames);
   applyParams();
   activated_ = true;
   return true;
}

void Plugin::deactivate() { activated_ = false; }

bool Plugin::startProcessing() {
   if (!activated_) return false;
   processing_ = true;
   return true;
}

void Plugin::stopProcessing() { processing_ = false; }

void Plugin::reset() { dsp_.reset(); }

void Plugin::applyParamEvent(const clap_event_param_value_t* ev) {
   if (ev->param_id == kParamQuality) {
      quality_ = static_cast<int>(ev->value);
   }
   applyParams();
}

clap_process_status Plugin::process(const clap_process_t* proc) {
   if (!processing_) return CLAP_PROCESS_ERROR;

   // Parameter events first.
   const std::uint32_t nEvents = proc->in_events->size(proc->in_events);
   for (std::uint32_t i = 0; i < nEvents; ++i) {
      const clap_event_header_t* h = proc->in_events->get(proc->in_events, i);
      if (h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_PARAM_VALUE) {
         applyParamEvent(reinterpret_cast<const clap_event_param_value_t*>(h));
      }
   }

   if (proc->audio_inputs_count == 0 || proc->audio_outputs_count == 0)
      return CLAP_PROCESS_CONTINUE;

   const clap_audio_buffer_t* in = &proc->audio_inputs[0];
   const clap_audio_buffer_t* out = &proc->audio_outputs[0];
   const std::uint32_t frames = proc->frames_count;

   const int nCh = static_cast<int>(std::min(out->channel_count, 2u));
   float* chans[2] = { nullptr, nullptr };
   for (int ch = 0; ch < nCh; ++ch) {
      const int srcCh = std::min(ch, static_cast<int>(in->channel_count) - 1);
      std::copy_n(in->data32[srcCh], frames, out->data32[ch]);
      chans[ch] = out->data32[ch];
   }

   dsp_.process(chans, nCh, frames);
   return CLAP_PROCESS_CONTINUE;
}

const void* Plugin::getExtension(const char* id) {
   if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &kParamsExt;
   if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &kStateExt;
   if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) return &kLatencyExt;
   if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &kAudioPortsExt;
   return nullptr;
}

void Plugin::onMainThread() {}

// ---- params ---------------------------------------------------------------

bool Plugin::paramsInfo(std::uint32_t index, clap_param_info_t* info) const {
   if (index != 0) return false;
   info->id = kParamQuality;
   info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
   info->cookie = nullptr;
   std::snprintf(info->name, sizeof(info->name), "Quality");
   std::snprintf(info->module, sizeof(info->module), "Codec");
   info->min_value = kQualityMin;
   info->max_value = kQualityMax;
   info->default_value = kQualityDefault;
   return true;
}

bool Plugin::paramsValue(clap_id id, double* out) const {
   if (id == kParamQuality) { *out = quality_; return true; }
   return false;
}

bool Plugin::paramsValueToText(clap_id id, double value, char* display, std::uint32_t size) const {
   if (id == kParamQuality) {
      std::snprintf(display, size, "%d", static_cast<int>(value));
      return true;
   }
   return false;
}

bool Plugin::paramsTextToValue(clap_id id, const char* display, double* out) const {
   if (id != kParamQuality) return false;
   int v = 0;
   if (std::sscanf(display, "%d", &v) != 1) return false;
   if (v < kQualityMin || v > kQualityMax) return false;
   *out = v;
   return true;
}

void Plugin::paramsFlush(const clap_input_events_t* in, const clap_output_events_t*) {
   const std::uint32_t n = in->size(in);
   for (std::uint32_t i = 0; i < n; ++i) {
      const clap_event_header_t* h = in->get(in, i);
      if (h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_PARAM_VALUE) {
         applyParamEvent(reinterpret_cast<const clap_event_param_value_t*>(h));
      }
   }
}

// ---- state ----------------------------------------------------------------

bool Plugin::stateSave(const clap_ostream_t* stream) {
   const std::int32_t data[2] = { quality_, 0 };
   const char* p = reinterpret_cast<const char*>(data);
   std::uint64_t remaining = sizeof(data);
   while (remaining > 0) {
      const std::int64_t w = stream->write(stream, p, remaining);
      if (w <= 0) return false;
      p += w;
      remaining -= static_cast<std::uint64_t>(w);
   }
   return true;
}

bool Plugin::stateLoad(const clap_istream_t* stream) {
   std::int32_t data[2] = { 0, 0 };
   char* p = reinterpret_cast<char*>(data);
   std::uint64_t remaining = sizeof(data);
   while (remaining > 0) {
      const std::int64_t r = stream->read(stream, p, remaining);
      if (r <= 0) return false;
      p += r;
      remaining -= static_cast<std::uint64_t>(r);
   }
   quality_ = data[0];
   applyParams();
   return true;
}

// ---- latency --------------------------------------------------------------

std::uint32_t Plugin::latencyGet() const { return dsp_.latency(); }

// ---- audio ports ----------------------------------------------------------

std::uint32_t Plugin::audioPortsCount(bool) const { return 1; }

bool Plugin::audioPortsInfo(std::uint32_t index, bool, clap_audio_port_info_t* info) const {
   if (index != 0) return false;
   info->id = 0;
   std::snprintf(info->name, sizeof(info->name), "Audio");
   info->flags = CLAP_AUDIO_PORT_IS_MAIN;
   info->channel_count = 2;
   info->port_type = CLAP_PORT_STEREO;
   info->in_place_pair = 0;
   return true;
}

} // namespace lovoip

extern "C" __declspec(dllexport) const clap_plugin_entry_t clap_entry = lovoip::kEntry;





