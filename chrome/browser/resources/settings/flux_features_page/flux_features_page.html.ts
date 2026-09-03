// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {SettingsFluxFeaturesPageElement} from './flux_features_page.js';

export function getHtml(this: SettingsFluxFeaturesPageElement) {
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
</settings-section>
<!--_html_template_end_-->`;
}
