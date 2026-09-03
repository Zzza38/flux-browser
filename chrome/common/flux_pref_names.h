// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_COMMON_FLUX_PREF_NAMES_H_
#define CHROME_COMMON_FLUX_PREF_NAMES_H_

namespace prefs {

// Whether Flux shows a hover picture-in-picture button on <video> elements.
inline constexpr char kFluxVideoPictureInPictureOverlayEnabled[] =
    "flux.video_picture_in_picture_overlay.enabled";

// Whether Flux cycles a rainbow color filter over the whole browser window.
inline constexpr char kFluxRgbModeEnabled[] = "flux.rgb_mode.enabled";

// How fast Flux RGB mode sweeps the color wheel, in degrees per second.
inline constexpr char kFluxRgbModeSpeed[] = "flux.rgb_mode.speed";

}  // namespace prefs

#endif  // CHROME_COMMON_FLUX_PREF_NAMES_H_
