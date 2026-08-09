#include "dsp.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lovoip {

namespace {
constexpr double kPi = 3.14159265358979323846;
} // namespace

void CeltSim::Biquad::setLowpass(double sampleRate, double freq, double q) {
   const double w0 = 2.0 * kPi * freq / sampleRate;
   const double c = std::cos(w0), s = std::sin(w0);
   const double alpha = s / (2.0 * q);
   const double a0 = 1.0 + alpha;
   b0 = (1.0 - c) * 0.5 / a0;
   b1 = (1.0 - c) / a0;
   b2 = b0;
   a1 = -2.0 * c / a0;
   a2 = (1.0 - alpha) / a0;
}

float CeltSim::Biquad::tick(float x) {
   const double y = b0 * x + z1;
   z1 = b1 * x - a1 * y + z2;
   z2 = b2 * x - a2 * y;
   return static_cast<float>(y);
}

void CeltSim::prepare(double sampleRate, std::uint32_t) {
   sampleRate_ = sampleRate;
   // Peak-envelope follower: fast attack (~1 ms) so transients are tracked,
   // slow release (~150 ms) so the quantization step stays stable between
   // transients (avoids the per-frame gain lurch).
   peakAttack_  = 1.0 - std::exp(-1.0 / (0.001 * sampleRate));
   peakRelease_ = 1.0 - std::exp(-1.0 / (0.150 * sampleRate));
   applySettings();
   reset();
}

void CeltSim::reset() {
   for (auto& c : chans_) {
      c.lp1.clear();
      c.lp2.clear();
      c.outLp1.clear();
      c.outLp2.clear();
      c.holdSample = 0.0f;
      c.holdSampleNext = 0.0f;
      c.holdPhase = 0.0;
   }
   peakEnv_ = 1e-4f;
}

void CeltSim::setQuality(int quality) {
   quality = std::max(8, std::min(22, quality));
   if (quality == quality_) return;
   quality_ = quality;
   applySettings();
}

void CeltSim::applySettings() {
   // The quality value is both the bandlimit (kHz) and the bitrate (kbps),
   // mirroring how Source's sv_voicequality couples the two.
   codecRate_ = std::min<double>(quality_ * 500.0 * 0.9 * 2.0, sampleRate_);
   const double lpFreq = std::min<double>(quality_ * 500.0 * 0.9, sampleRate_ * 0.45);

   // Map quality 8..22 to degradation depth (8 = grittiest, 22 = cleanest).
   const double t = (quality_ - 8.0) / (22.0 - 8.0); // 8->0, 22->1
   quantLevels_ = std::pow(2.0, 2.0 + t * 8.0); // 4 .. 1024 levels (2..10 bit)
   noiseLevel_ = 0.0012 * (1.0 - t);

   for (auto& c : chans_) {
      c.lp1.setLowpass(sampleRate_, lpFreq, 0.7071);
      c.lp2.setLowpass(sampleRate_, lpFreq, 0.7071);
      c.outLp1.setLowpass(sampleRate_, lpFreq, 0.7071);
      c.outLp2.setLowpass(sampleRate_, lpFreq, 0.7071);
   }
}

void CeltSim::process(float* const* channels, int numChannels, std::uint32_t frames) {
   const int nCh = std::min(numChannels, kMaxChannels);
   const bool resampling = codecRate_ < sampleRate_ - 0.5;
   const double holdStep = codecRate_ / sampleRate_;
   const float levels = static_cast<float>(quantLevels_);

   for (std::uint32_t i = 0; i < frames; ++i) {
      // Track a shared peak envelope from the loudest channel.
      float blockPeak = 0.0f;
      for (int ch = 0; ch < nCh; ++ch) {
         const float a = std::fabs(channels[ch][i]);
         if (a > blockPeak) blockPeak = a;
      }
      const double coef = blockPeak > peakEnv_ ? peakAttack_ : peakRelease_;
      peakEnv_ += static_cast<float>((blockPeak - peakEnv_) * coef);
      const float step = std::max(peakEnv_, 1e-5f) * 2.0f / levels;

      for (int ch = 0; ch < nCh; ++ch) {
         ChannelState& st = chans_[ch];
         float x = st.lp1.tick(channels[ch][i]);
         x = st.lp2.tick(x);

         float held = x;
         if (resampling) {
            st.holdPhase += holdStep;
            while (st.holdPhase >= 1.0) {
               st.holdPhase -= 1.0;
               st.holdSample = st.holdSampleNext;
               st.holdSampleNext = x;
            }
            held = st.holdSample + (st.holdSampleNext - st.holdSample) * static_cast<float>(st.holdPhase);
         }

         float y = std::floor(held / step + 0.5f) * step;
         if (noiseLevel_ > 0.0) {
            noiseState_ = noiseState_ * 1664525u + 1013904223u;
            const std::uint32_t bits = (noiseState_ >> 9) | 0x3f800000u;
            float asFloat;
            std::memcpy(&asFloat, &bits, sizeof(float));
            y += (asFloat - 1.5f) * static_cast<float>(noiseLevel_) * peakEnv_;
         }
         y = st.outLp1.tick(y);
         y = st.outLp2.tick(y);
         channels[ch][i] = y;
      }
   }
}

} // namespace lovoip

