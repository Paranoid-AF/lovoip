#pragma once

#include <cstdint>
#include <cstring>

namespace lovoip {

// Simulates the Source Engine vaudio_celt voice codec character:
// bandlimiting to the codec rate, decimation/resampling at that rate, and
// adaptive quantization against a smoothed signal envelope (the low-bitrate
// CELT grit). Processes sample-by-sample with no frame blocking, so it stays
// stable at any host block size.
//
// Real-time safe: no allocation or blocking in process().
class CeltSim {
public:
   static constexpr int kMaxChannels = 2;

   // Prepares for the given sample rate (maxBlockFrames is unused; kept for a
   // stable call signature).
   void prepare(double sampleRate, std::uint32_t maxBlockFrames);

   // Resets all filter state (e.g. on transport jump).
   void reset();

   // Single quality control: value is both the bandlimit (kHz) and bitrate
   // (kbps). Range 8..22.
   void setQuality(int quality);
   int  quality() const { return quality_; }

   // No added latency: the signal path is sample-synchronous.
   std::uint32_t latency() const { return 0; }

   // In-place processing. channels[i] must hold at least frames floats.
   void process(float* const* channels, int numChannels, std::uint32_t frames);

private:
   struct Biquad {
      double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
      double z1 = 0.0, z2 = 0.0;

      void setLowpass(double sampleRate, double freq, double q);
      float tick(float x);
      void clear() { z1 = z2 = 0.0; }
   };

   struct ChannelState {
      Biquad lp1, lp2;          // encode bandlimit cascade
      Biquad outLp1, outLp2;    // decode bandlimit cascade
      float  holdSample = 0.0f;     // previous codec-rate sample (lerp)
      float  holdSampleNext = 0.0f; // current codec-rate sample (lerp target)
      double holdPhase  = 0.0;
   };

   void applySettings();

   double sampleRate_ = 48000.0;
   int    quality_ = 22;

   // codec rate the signal is decimated to (<= sampleRate_)
   double codecRate_    = 22050.0;
   double quantLevels_  = 256.0;  // quantization levels
   double noiseLevel_   = 0.0;    // comfort-noise floor

   // Smoothed peak envelope (shared across channels) used to size the
   // quantization step, so quiet passages are not crushed to silence.
   float  peakEnv_    = 1e-4f;
   double peakAttack_  = 0.0; // coefficients set in applySettings
   double peakRelease_ = 0.0;

   ChannelState chans_[kMaxChannels];

   // comfort noise LCG state
   std::uint32_t noiseState_ = 0x12345678u;
};

} // namespace lovoip

