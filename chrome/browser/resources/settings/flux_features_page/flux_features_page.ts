// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import '../controls/settings_toggle_button.js';
import '../settings_page/settings_section.js';

import {getCss as getCrSharedStyle} from 'chrome://resources/cr_elements/cr_shared_style_lit.css.js';
import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import {getSearchManager} from '../search_settings.js';
import type {SettingsPlugin} from '../settings_main/settings_plugin.js';
import {getCss as getSettingsSharedLit} from '../settings_shared_lit.css.js';

import {getHtml} from './flux_features_page.html.js';

export class SettingsFluxFeaturesPageElement extends CrLitElement implements
    SettingsPlugin {
  static get is() {
    return 'settings-flux-features-page';
  }

  static override get styles() {
    return [getCrSharedStyle(), getSettingsSharedLit()];
  }

  override render() {
    return getHtml.bind(this)();
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
