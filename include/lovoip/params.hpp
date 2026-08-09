#pragma once

#include <cstdint>

namespace lovoip {

// CLAP plugin identity
inline constexpr const char* kPluginId      = "com.lovoip.celt";
inline constexpr const char* kPluginName    = "lovoip";
inline constexpr const char* kPluginVendor  = "lovoip";
inline constexpr const char* kPluginVersion = "0.1.0";

// Parameter ids
inline constexpr std::uint32_t kParamQuality = 1;

// Single quality control. The value is both the codec bandlimit in kHz and
// the bitrate in kbps (e.g. 22 -> 22 kHz bandlimit at 22 kbps), mirroring how
// Source's sv_voicequality couples the two.
inline constexpr int kQualityMin     = 8;
inline constexpr int kQualityMax     = 22;
inline constexpr int kQualityDefault = 22;

} // namespace lovoip
