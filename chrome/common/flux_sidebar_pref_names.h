// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_COMMON_FLUX_SIDEBAR_PREF_NAMES_H_
#define CHROME_COMMON_FLUX_SIDEBAR_PREF_NAMES_H_

namespace prefs {

// Whether the Flux sidebar is shown in normal browser windows.
inline constexpr char kFluxSidebarEnabled[] = "flux.sidebar.enabled";

// List of sites pinned to the Flux sidebar. This pref also preserves the site
// order and each site's most recently committed URL.
inline constexpr char kFluxSidebarSites[] = "flux.sidebar.sites";

// Width of the open Flux sidebar panel in device-independent pixels.
inline constexpr char kFluxSidebarWidth[] = "flux.sidebar.width";

// Whether the Flux sidebar's two-site split is vertical.
inline constexpr char kFluxSidebarSplitVertical[] =
    "flux.sidebar.split_vertical";

}  // namespace prefs

#endif  // CHROME_COMMON_FLUX_SIDEBAR_PREF_NAMES_H_
