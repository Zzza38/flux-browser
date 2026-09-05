// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import '../controls/settings_slider.js';
import '../controls/settings_toggle_button.js';
import '../settings_page/settings_section.js';

import {PrefServiceObserverMixinLit} from '/shared/settings/prefs2/pref_service_observer_mixin_lit.js';
import {getCss as getCrSharedStyle} from 'chrome://resources/cr_elements/cr_shared_style_lit.css.js';
import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import {getSearchManager} from '../search_settings.js';
import type {SettingsPlugin} from '../settings_main/settings_plugin.js';
import {getCss as getSettingsSharedLit} from '../settings_shared_lit.css.js';

import {getHtml} from './flux_features_page.html.js';

const SettingsFluxFeaturesPageElementBase =
    PrefServiceObserverMixinLit(CrLitElement);

export class SettingsFluxFeaturesPageElement extends
    SettingsFluxFeaturesPageElementBase implements SettingsPlugin {
  static get is() {
    return 'settings-flux-features-page';
  }

  static override get styles() {
    return [getCrSharedStyle(), getSettingsSharedLit()];
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      rgbModeEnabled_: {type: Boolean},
    };
  }

  // Mirrors flux.rgb_mode.enabled so the speed slider only shows while RGB
  // mode is on.
  protected accessor rgbModeEnabled_: boolean = false;

  override connectedCallback() {
    super.connectedCallback();

    this.addPrefObserver<boolean>('flux.rgb_mode.enabled', pref => {
      this.rgbModeEnabled_ = pref.value;
    });
  }

  async searchContents(query: string) {
    const searchRequest = await getSearchManager().search(query, this);
    return searchRequest.getSearchResult();
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-flux-features-page': SettingsFluxFeaturesPageElement;
  }
}

customElements.define(
    SettingsFluxFeaturesPageElement.is, SettingsFluxFeaturesPageElement);
