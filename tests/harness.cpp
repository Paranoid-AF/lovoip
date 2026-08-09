// lovoip-harness: minimal offline CLAP host.
//
// Loads lovoip.clap, renders a mono/stereo WAV through it with the given
// quality setting and writes a 16-bit WAV. No dependencies beyond the CRT.
//
// Usage: lovoip-harness.exe <in.wav> <out.wav> [quality]
// Exit codes: 0 ok, 1 usage, 2 load/instantiate error, 3 WAV I/O error,
//             4 self-check failure.

#include <clap/clap.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#define _CRT_SECURE_NO_WARNINGS
#include <cstring>
#include <vector>

#define NOMINMAX
#include <windows.h>

namespace {

struct WavData {
   std::uint32_t sampleRate = 0;
   std::uint16_t channels = 0;
   std::vector<float> samples; // interleaved
};

std::uint32_t readU32(const unsigned char* p) {
   return p[0] | (p[1] << 8) | (p[2] << 16) | (std::uint32_t(p[3]) << 24);
}
std::uint16_t readU16(const unsigned char* p) { return p[0] | (p[1] << 8); }

bool loadWav(const char* path, WavData& out) {
   FILE* f = nullptr;
   if (fopen_s(&f, path, "rb") != 0 || !f) return false;
   fseek(f, 0, SEEK_END);
   const long size = ftell(f);
   fseek(f, 0, SEEK_SET);
   std::vector<unsigned char> buf(size);
   if (fread(buf.data(), 1, size, f) != static_cast<size_t>(size)) { fclose(f); return false; }
   fclose(f);
   if (size < 12 || std::memcmp(buf.data(), "RIFF", 4) || std::memcmp(buf.data() + 8, "WAVE", 4))
      return false;

   bool haveFmt = false, haveData = false;
   std::uint16_t bitsPerSample = 0, format = 0;
   size_t dataOff = 0;
   std::uint32_t dataSize = 0;
   for (size_t off = 12; off + 8 <= buf.size();) {
      const std::uint32_t chunkSize = readU32(&buf[off + 4]);
      if (!std::memcmp(&buf[off], "fmt ", 4) && chunkSize >= 16) {
         format = readU16(&buf[off + 8]);
         out.channels = readU16(&buf[off + 10]);
         out.sampleRate = readU32(&buf[off + 12]);
         bitsPerSample = readU16(&buf[off + 22]);
         haveFmt = true;
      } else if (!std::memcmp(&buf[off], "data", 4)) {
         dataOff = off + 8;
         dataSize = std::min<std::uint32_t>(chunkSize, static_cast<std::uint32_t>(buf.size() - dataOff));
         haveData = true;
      }
      off += 8 + chunkSize + (chunkSize & 1);
   }
   if (!haveFmt || !haveData || out.channels == 0) return false;

   const size_t frames = dataSize / (out.channels * (bitsPerSample / 8));
   out.samples.resize(frames * out.channels);
   if (format == 3 && bitsPerSample == 32) {
      std::memcpy(out.samples.data(), &buf[dataOff], frames * out.channels * sizeof(float));
   } else if (format == 1 && bitsPerSample == 16) {
      const std::int16_t* src = reinterpret_cast<const std::int16_t*>(&buf[dataOff]);
      for (size_t i = 0; i < frames * out.channels; ++i)
         out.samples[i] = src[i] / 32768.0f;
   } else {
      return false;
   }
   return true;
}

bool saveWav(const char* path, const float* samples, std::uint32_t frames, std::uint16_t channels,
             std::uint32_t sampleRate) {
   FILE* f = nullptr;
   if (fopen_s(&f, path, "wb") != 0 || !f) return false;
   const std::uint32_t dataSize = frames * channels * sizeof(std::int16_t);
   const std::uint32_t riffSize = 36 + dataSize;

   auto writeBytes = [&](const void* p, size_t n) { return fwrite(p, 1, n, f) == n; };
   auto writeU32 = [&](std::uint32_t v) {
      const unsigned char b[4] = { static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8),
                                   static_cast<unsigned char>(v >> 16), static_cast<unsigned char>(v >> 24) };
      return writeBytes(b, 4);
   };
   auto writeU16 = [&](std::uint16_t v) {
      const unsigned char b[2] = { static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8) };
      return writeBytes(b, 2);
   };

   bool ok = writeBytes("RIFF", 4) && writeU32(riffSize) && writeBytes("WAVE", 4) &&
             writeBytes("fmt ", 4) && writeU32(16) && writeU16(1) && writeU16(channels) &&
             writeU32(sampleRate) && writeU32(sampleRate * channels * 2) &&
             writeU16(static_cast<std::uint16_t>(channels * 2)) && writeU16(16) &&
             writeBytes("data", 4) && writeU32(dataSize);
   for (size_t i = 0; ok && i < static_cast<size_t>(frames) * channels; ++i) {
      float v = samples[i];
      v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
      const std::int16_t s = static_cast<std::int16_t>(std::lround(v * 32767.0f));
      ok = writeU16(static_cast<std::uint16_t>(s));
   }
   fclose(f);
   return ok;
}

// --- Minimal event list for parameter events ---
struct EventList {
   clap_input_events_t list{};
   std::vector<clap_event_param_value_t> events;

   EventList() {
      list.size = [](const clap_input_events_t* l) -> std::uint32_t {
         return static_cast<std::uint32_t>(static_cast<EventList*>(l->ctx)->events.size());
      };
      list.get = [](const clap_input_events_t* l, std::uint32_t i) -> const clap_event_header_t* {
         auto* self = static_cast<EventList*>(l->ctx);
         return i < self->events.size() ? &self->events[i].header : nullptr;
      };
      list.ctx = this;
   }

   void pushParamValue(std::uint32_t time, clap_id id, double value) {
      clap_event_param_value_t ev{};
      ev.header.size = sizeof(ev);
      ev.header.time = time;
      ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
      ev.header.type = CLAP_EVENT_PARAM_VALUE;
      ev.header.flags = 0;
      ev.param_id = id;
      ev.cookie = nullptr;
      ev.note_id = -1;
      ev.port_index = -1;
      ev.channel = -1;
      ev.key = -1;
      ev.value = value;
      events.push_back(ev);
   }
};

// Minimal output event list that accepts everything.
bool CLAP_ABI outTryPush(const clap_output_events_t*, const clap_event_header_t*) { return true; }
clap_output_events_t g_outEvents = { nullptr, outTryPush };

// Minimal host vtable.
const void* CLAP_ABI hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void CLAP_ABI hostRequestRestart(const clap_host_t*) {}
void CLAP_ABI hostRequestProcess(const clap_host_t*) {}
void CLAP_ABI hostRequestCallback(const clap_host_t*) {}

clap_host_t g_host = {
   CLAP_VERSION_INIT,
   nullptr, // host_data
   "lovoip-harness",
   "lovoip",
   "",
   "0.1.0",
   hostGetExtension,
   hostRequestRestart,
   hostRequestProcess,
   hostRequestCallback,
};

} // namespace

int main(int argc, char** argv) {
   if (argc < 3 || argc > 4) {
      std::fprintf(stderr, "usage: lovoip-harness.exe <in.wav> <out.wav> [quality]\n");
      return 1;
   }
   const char* inPath = argv[1];
   const char* outPath = argv[2];
   const int quality = argc > 3 ? std::atoi(argv[3]) : 22;

   // Self-check mode: in.wav == "--selftest" renders a silence buffer and
   // verifies silence-in -> silence-out.
   const bool selfTest = std::strcmp(inPath, "--selftest") == 0;

   WavData wav;
   if (!selfTest && !loadWav(inPath, wav)) {
      std::fprintf(stderr, "error: cannot read WAV '%s'\n", inPath);
      return 3;
   }
   if (selfTest) {
      wav.sampleRate = 48000;
      wav.channels = 1;
      wav.samples.assign(48000, 0.0f); // 1 s of silence
   }

   char pluginPath[MAX_PATH];
   GetModuleFileNameA(nullptr, pluginPath, MAX_PATH);
   char* slash = std::strrchr(pluginPath, '\\');
   if (slash) *slash = '\0';
   strcat_s(pluginPath, MAX_PATH, "\\lovoip.clap");

   HMODULE lib = LoadLibraryA(pluginPath);
   if (!lib) {
      std::fprintf(stderr, "error: cannot load '%s' (%lu)\n", pluginPath, GetLastError());
      return 2;
   }
   const auto* entry = reinterpret_cast<const clap_plugin_entry_t*>(GetProcAddress(lib, "clap_entry"));
   if (!entry || !clap_version_is_compatible(entry->clap_version)) {
      std::fprintf(stderr, "error: no compatible clap_entry in '%s'\n", pluginPath);
      return 2;
   }
   if (!entry->init(pluginPath)) {
      std::fprintf(stderr, "error: clap_entry.init failed\n");
      return 2;
   }
   const auto* factory = static_cast<const clap_plugin_factory_t*>(
       entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
   if (!factory || factory->get_plugin_count(factory) != 1) {
      std::fprintf(stderr, "error: unexpected factory state\n");
      return 2;
   }
   const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, 0);
   std::printf("plugin: %s (%s) v%s by %s\n", desc->name, desc->id, desc->version, desc->vendor);

   const clap_plugin_t* plugin = factory->create_plugin(factory, &g_host, desc->id);
   if (!plugin || !plugin->init(plugin)) {
      std::fprintf(stderr, "error: cannot instantiate plugin\n");
      return 2;
   }

   const std::uint32_t blockSize = 512;
   const double sampleRate = wav.sampleRate;
   if (!plugin->activate(plugin, sampleRate, blockSize, blockSize) || !plugin->start_processing(plugin)) {
      std::fprintf(stderr, "error: activate/start_processing failed\n");
      return 2;
   }

   // Set the parameters.
   const auto* params = static_cast<const clap_plugin_params_t*>(
       plugin->get_extension(plugin, CLAP_EXT_PARAMS));
   if (!params) {
      std::fprintf(stderr, "error: plugin has no params extension\n");
      return 2;
   }
   EventList paramEvents;
   paramEvents.pushParamValue(0, 1, quality); // lovoip::kParamQuality
   params->flush(plugin, &paramEvents.list, &g_outEvents);

   double gotQuality = 0;
   params->get_value(plugin, 1, &gotQuality);
   std::printf("param: quality=%d\n", static_cast<int>(gotQuality));
   if (static_cast<int>(gotQuality) != quality) {
      std::fprintf(stderr, "error: parameter round-trip mismatch\n");
      return 4;
   }

   const auto* latency = static_cast<const clap_plugin_latency_t*>(
       plugin->get_extension(plugin, CLAP_EXT_LATENCY));
   const std::uint32_t pluginLatency = latency ? latency->get(plugin) : 0;
   std::printf("latency: %u samples\n", pluginLatency);

   // Render. Match the channel count to the input so we never emit a silent
   // second channel (which would halve perceived level on mono-sum).
   const std::uint32_t channels = wav.channels;
   std::vector<float> outSamples(wav.samples.size() / wav.channels * channels, 0.0f);
   std::vector<std::vector<float>> inCh(channels, std::vector<float>(blockSize, 0.0f));
   std::vector<std::vector<float>> outCh(channels, std::vector<float>(blockSize, 0.0f));
   float inDummy[1] = { 0.0f };
   float outDummy[1] = { 0.0f };
   float* inPtr[2] = { inCh[0].data(), channels > 1 ? inCh[1].data() : inDummy };
   float* outPtr[2] = { outCh[0].data(), channels > 1 ? outCh[1].data() : outDummy };

   clap_audio_buffer_t inBuf{};
   inBuf.data32 = inPtr;
   inBuf.channel_count = channels;
   inBuf.latency = 0;
   inBuf.constant_mask = 0;
   clap_audio_buffer_t outBuf{};
   outBuf.data32 = outPtr;
   outBuf.channel_count = channels;
   outBuf.latency = 0;
   outBuf.constant_mask = 0;

   const size_t inFrames = wav.samples.size() / wav.channels;
   // Render input plus latency worth of tail so delayed audio isn't truncated.
   const size_t totalFrames = inFrames + pluginLatency + blockSize;
   size_t outFrame = 0;
   for (size_t pos = 0; pos < totalFrames; pos += blockSize) {
      for (std::uint32_t ch = 0; ch < channels; ++ch) {
         for (std::uint32_t i = 0; i < blockSize; ++i) {
            const size_t f = pos + i;
            const size_t srcCh = wav.channels == 1 ? 0 : ch;
            inCh[ch][i] = f < inFrames ? wav.samples[f * wav.channels + srcCh] : 0.0f;
         }
      }
      clap_process_t proc{};
      proc.steady_time = static_cast<std::int64_t>(pos);
      proc.frames_count = blockSize;
      proc.transport = nullptr;
      proc.audio_inputs = &inBuf;
      proc.audio_outputs = &outBuf;
      proc.audio_inputs_count = 1;
      proc.audio_outputs_count = 1;
      proc.in_events = &paramEvents.list;
      proc.out_events = &g_outEvents;
      const clap_process_status st = plugin->process(plugin, &proc);
      if (st != CLAP_PROCESS_CONTINUE && st != CLAP_PROCESS_CONTINUE_IF_NOT_QUIET) {
         std::fprintf(stderr, "error: process returned %d\n", st);
         return 2;
      }
      for (std::uint32_t ch = 0; ch < channels; ++ch)
         for (std::uint32_t i = 0; i < blockSize && outFrame < outSamples.size() / channels; ++i, ++outFrame)
            outSamples[outFrame * channels + ch] = outCh[ch][i];
   }

   plugin->stop_processing(plugin);
   plugin->deactivate(plugin);
   plugin->destroy(plugin);
   entry->deinit();
   FreeLibrary(lib);

   if (selfTest) {
      float peak = 0.0f;
      for (float v : outSamples) peak = std::max(peak, std::fabs(v));
      std::printf("self-test: silence in -> peak %.6f out\n", peak);
      return peak < 1e-4f ? 0 : 4;
   }

   if (!saveWav(outPath, outSamples.data(), static_cast<std::uint32_t>(outSamples.size() / channels),
                static_cast<std::uint16_t>(channels), static_cast<std::uint32_t>(sampleRate))) {
      std::fprintf(stderr, "error: cannot write WAV '%s'\n", outPath);
      return 3;
   }
   std::printf("wrote %s\n", outPath);
   return 0;
}




