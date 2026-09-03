// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {SettingsFluxFeaturesPageElement} from './flux_features_page.js';

export function getHtml(this: SettingsFluxFeaturesPageElement) {
  // clang-format off
  return html`<!--_html_template_start_-->
<settings-section page-title="$i18n{fluxFeaturesPageTitle}"
    class="cr-centered-card-container">
  <settings-toggle-button id="sidebarEnabled"
      pref-key="flux.sidebar.enabled"
      label="$i18n{fluxSidebarEnabledLabel}"
      sub-label="$i18n{fluxSidebarEnabledSubLabel}">
  </settings-toggle-button>
  <settings-toggle-button id="videoPipOverlayEnabled"
      pref-key="flux.video_picture_in_picture_overlay.enabled"
      label="$i18n{fluxVideoPipOverlayEnabledLabel}"
      sub-label="$i18n{fluxVideoPipOverlayEnabledSubLabel}">
  </settings-toggle-button>
  <settings-toggle-button id="rgbModeEnabled"
      pref-key="flux.rgb_mode.enabled"
      label="$i18n{fluxRgbModeEnabledLabel}"
      sub-label="$i18n{fluxRgbModeEnabledSubLabel}">
  </settings-toggle-button>
  ${this.rgbModeEnabled_ ? html`
    <div class="cr-row continuation" id="rgbModeSpeedRow">
      <div class="flex cr-padded-text" aria-hidden="true">
        $i18n{fluxRgbModeSpeedLabel}
      </div>
      <settings-slider id="rgbModeSpeed"
          pref-key="flux.rgb_mode.speed"
          min="15" max="720"
          label-aria="$i18n{fluxRgbModeSpeedLabel}"
          label-min="$i18n{fluxRgbModeSpeedSlow}"
          label-max="$i18n{fluxRgbModeSpeedFast}">
      </settings-slider>
    </div>
  ` : ''}
</settings-section>
<!--_html_template_end_-->`;
  // clang-format on
}
