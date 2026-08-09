#pragma once

#include <clap/clap.h>

#include "lovoip/params.hpp"
#include "dsp.hpp"

namespace lovoip {

// Concrete plugin object. Owns the clap_plugin_t vtable and all state.
class Plugin {
public:
   explicit Plugin(const clap_host_t* host);

   clap_plugin_t* clapPlugin() { return &plugin_; }

   // clap_plugin_t callbacks
   bool init();
   void destroy();
   bool activate(double sampleRate, std::uint32_t minFrames, std::uint32_t maxFrames);
   void deactivate();
   bool startProcessing();
   void stopProcessing();
   void reset();
   clap_process_status process(const clap_process_t* process);
   const void* getExtension(const char* id);
   void onMainThread();

   // params extension
   std::uint32_t paramsCount() const { return 1; }
   bool paramsInfo(std::uint32_t index, clap_param_info_t* info) const;
   bool paramsValue(clap_id id, double* out) const;
   bool paramsValueToText(clap_id id, double value, char* display, std::uint32_t size) const;
   bool paramsTextToValue(clap_id id, const char* display, double* out) const;
   void paramsFlush(const clap_input_events_t* in, const clap_output_events_t* out);

   // state extension
   bool stateSave(const clap_ostream_t* stream);
   bool stateLoad(const clap_istream_t* stream);

   // latency extension
   std::uint32_t latencyGet() const;

   // audio-ports extension
   std::uint32_t audioPortsCount(bool isInput) const;
   bool audioPortsInfo(std::uint32_t index, bool isInput, clap_audio_port_info_t* info) const;

   void applyParamEvent(const clap_event_param_value_t* ev);

   const clap_host_t* host() const { return host_; }

private:
   clap_plugin_t   plugin_{};
   const clap_host_t* host_ = nullptr;
   const clap_host_log_t* hostLog_ = nullptr;

   CeltSim dsp_;
   double  sampleRate_ = 48000.0;
   bool    activated_ = false;
   bool    processing_ = false;

   int quality_ = kQualityDefault;

   void applyParams();
};

} // namespace lovoip

