// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FLUX_RGB_MODE_FLUX_RGB_MODE_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_FLUX_RGB_MODE_FLUX_RGB_MODE_CONTROLLER_H_

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "cc/paint/filter_operation.h"
#include "components/prefs/pref_change_registrar.h"
#include "ui/compositor/compositor_animation_observer.h"

class PrefService;

namespace ui {
class Compositor;
}

namespace views {
class Widget;
}

namespace flux_rgb_mode {

// Builds the color matrix that tints everything toward `hue_degrees` on the
// color wheel. Exposed for tests.
cc::FilterOperation::Matrix BuildRgbColorMatrix(float hue_degrees);

// Advances a hue by `speed_degrees_per_second` over `elapsed` and wraps it into
// [0, 360). Exposed for tests.
float AdvanceHue(float hue_degrees,
                 int speed_degrees_per_second,
                 base::TimeDelta elapsed);

// While the Flux RGB mode pref is on, sweeps a color filter across the browser
// window's compositor layer once per frame. Because the filter lives on the
// widget's root layer, it covers everything the window draws: tab strip,
// toolbar, bookmarks, and web contents.
class FluxRgbModeController : public ui::CompositorAnimationObserver {
 public:
  FluxRgbModeController(views::Widget* widget, PrefService* prefs);
  FluxRgbModeController(const FluxRgbModeController&) = delete;
  FluxRgbModeController& operator=(const FluxRgbModeController&) = delete;
  ~FluxRgbModeController() override;

  // ui::CompositorAnimationObserver:
  void OnAnimationStep(base::TimeTicks timestamp) override;
  void OnCompositingShuttingDown(ui::Compositor* compositor) override;

 private:
  // Starts or stops the sweep to match the current pref value.
  void OnEnabledChanged();

  void StartAnimating();
  void StopAnimating();

  // Removes the color filter so the window paints its normal colors again.
  void ClearFilter();

  const raw_ptr<views::Widget> widget_;
  const raw_ptr<PrefService> prefs_;
  PrefChangeRegistrar pref_change_registrar_;

  // Non-null only while this is registered as an animation observer.
  raw_ptr<ui::Compositor> observed_compositor_ = nullptr;

  // Current position on the color wheel, in degrees.
  float hue_degrees_ = 0.0f;

  // Timestamp of the previous animation step, used to keep the sweep speed
  // independent of the frame rate.
  base::TimeTicks last_step_;
};

}  // namespace flux_rgb_mode

#endif  // CHROME_BROWSER_UI_VIEWS_FLUX_RGB_MODE_FLUX_RGB_MODE_CONTROLLER_H_
