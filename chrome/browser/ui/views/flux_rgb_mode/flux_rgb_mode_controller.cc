// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/flux_rgb_mode/flux_rgb_mode_controller.h"

#include <algorithm>
#include <cmath>

#include "base/functional/bind.h"
#include "chrome/common/flux_pref_names.h"
#include "components/prefs/pref_service.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer.h"
#include "ui/views/widget/widget.h"

namespace flux_rgb_mode {

namespace {

// How much of each pixel is replaced by the cycling tint. The rest keeps the
// window's real colors so text stays readable.
constexpr float kTintStrength = 0.55f;

// Rec. 709 luminance weights, used to pick how bright each tinted pixel is.
constexpr float kLuminanceR = 0.2126f;
constexpr float kLuminanceG = 0.7152f;
constexpr float kLuminanceB = 0.0722f;

// Slowest and fastest sweeps the settings slider can ask for, in degrees per
// second. These bracket whatever the pref holds, since a corrupt or
// out-of-range value would otherwise stall or strobe the window.
constexpr int kMinSpeedDegreesPerSecond = 15;
constexpr int kMaxSpeedDegreesPerSecond = 720;

// A single frame can be arbitrarily late if the window was occluded or the
// machine was asleep. Cap the step so the hue never jumps by a large amount.
constexpr base::TimeDelta kMaxStep = base::Milliseconds(100);

}  // namespace

cc::FilterOperation::Matrix BuildRgbColorMatrix(float hue_degrees) {
  const SkScalar hsv[3] = {
      std::fmod(std::fmod(hue_degrees, 360.0f) + 360.0f, 360.0f), 1.0f, 1.0f};
  const SkColor tint = SkHSVToColor(hsv);
  const float tint_r = SkColorGetR(tint) / 255.0f;
  const float tint_g = SkColorGetG(tint) / 255.0f;
  const float tint_b = SkColorGetB(tint) / 255.0f;

  // Blend the identity matrix with a colorize matrix that maps every pixel's
  // luminance onto the tint.
  const float keep = 1.0f - kTintStrength;
  const float mix = kTintStrength;
  cc::FilterOperation::Matrix matrix = {};
  matrix[0] = keep + mix * kLuminanceR * tint_r;
  matrix[1] = mix * kLuminanceG * tint_r;
  matrix[2] = mix * kLuminanceB * tint_r;
  matrix[5] = mix * kLuminanceR * tint_g;
  matrix[6] = keep + mix * kLuminanceG * tint_g;
  matrix[7] = mix * kLuminanceB * tint_g;
  matrix[10] = mix * kLuminanceR * tint_b;
  matrix[11] = mix * kLuminanceG * tint_b;
  matrix[12] = keep + mix * kLuminanceB * tint_b;
  // Leave alpha alone.
  matrix[18] = 1.0f;
  return matrix;
}

float AdvanceHue(float hue_degrees,
                 int speed_degrees_per_second,
                 base::TimeDelta elapsed) {
  const int speed =
      std::clamp(speed_degrees_per_second, kMinSpeedDegreesPerSecond,
                 kMaxSpeedDegreesPerSecond);
  const base::TimeDelta step = std::clamp(elapsed, base::TimeDelta(), kMaxStep);
  const float advanced = hue_degrees + speed * step.InSecondsF();
  return std::fmod(std::fmod(advanced, 360.0f) + 360.0f, 360.0f);
}

FluxRgbModeController::FluxRgbModeController(views::Widget* widget,
                                             PrefService* prefs)
    : widget_(widget), prefs_(prefs) {
  // The sweep is meant to run for as long as the pref is on, so opt out of the
  // long-running animation check.
  set_check_active_duration(false);

  pref_change_registrar_.Init(prefs_);
  pref_change_registrar_.Add(
      prefs::kFluxRgbModeEnabled,
      base::BindRepeating(&FluxRgbModeController::OnEnabledChanged,
                          base::Unretained(this)));
  OnEnabledChanged();
}

FluxRgbModeController::~FluxRgbModeController() {
  StopAnimating();
}

void FluxRgbModeController::OnAnimationStep(base::TimeTicks timestamp) {
  ui::Layer* layer = widget_->GetLayer();
  if (!layer) {
    return;
  }

  if (last_step_.is_null()) {
    last_step_ = timestamp;
  }
  hue_degrees_ =
      AdvanceHue(hue_degrees_, prefs_->GetInteger(prefs::kFluxRgbModeSpeed),
                 timestamp - last_step_);
  last_step_ = timestamp;

  layer->SetLayerCustomColorMatrix(BuildRgbColorMatrix(hue_degrees_));
}

void FluxRgbModeController::OnCompositingShuttingDown(
    ui::Compositor* compositor) {
  if (observed_compositor_ == compositor) {
    compositor->RemoveAnimationObserver(this);
    observed_compositor_ = nullptr;
  }
}

void FluxRgbModeController::OnEnabledChanged() {
  if (prefs_->GetBoolean(prefs::kFluxRgbModeEnabled)) {
    StartAnimating();
  } else {
    StopAnimating();
  }
}

void FluxRgbModeController::StartAnimating() {
  if (observed_compositor_) {
    return;
  }
  ui::Compositor* compositor = widget_->GetCompositor();
  if (!compositor) {
    return;
  }
  observed_compositor_ = compositor;
  last_step_ = base::TimeTicks();
  compositor->AddAnimationObserver(this);
}

void FluxRgbModeController::StopAnimating() {
  if (observed_compositor_) {
    observed_compositor_->RemoveAnimationObserver(this);
    observed_compositor_ = nullptr;
  }
  ClearFilter();
}

void FluxRgbModeController::ClearFilter() {
  ui::Layer* layer = widget_->GetLayer();
  if (layer && layer->LayerHasCustomColorMatrix()) {
    layer->ClearLayerCustomColorMatrix();
  }
}

}  // namespace flux_rgb_mode
