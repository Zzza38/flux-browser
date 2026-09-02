// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FLUX_SIDEBAR_FLUX_SIDEBAR_MODEL_H_
#define CHROME_BROWSER_UI_VIEWS_FLUX_SIDEBAR_FLUX_SIDEBAR_MODEL_H_

#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "components/prefs/pref_change_registrar.h"
#include "url/gurl.h"

class PrefService;

namespace flux_sidebar {

struct FluxSite {
  std::string id;
  std::u16string name;
  GURL url;
  std::u16string mark;
  std::string color;
  bool muted = false;
  // User-chosen icon as a "data:image/png;base64,..." URL. Remote images are
  // fetched and re-encoded when they are chosen rather than stored by
  // reference, so an icon keeps working offline and if its source goes away.
  // Empty means "fall back to the favicon, then the letter mark".
  std::string icon;

  bool operator==(const FluxSite&) const = default;
};

// Profile-backed, syncable list of websites pinned to the Flux rail.
class FluxSidebarModel {
 public:
  explicit FluxSidebarModel(PrefService* prefs);
  FluxSidebarModel(const FluxSidebarModel&) = delete;
  FluxSidebarModel& operator=(const FluxSidebarModel&) = delete;
  ~FluxSidebarModel();

  const std::vector<FluxSite>& sites() const { return sites_; }
  const FluxSite* FindSite(const std::string& id) const;

  bool AddSite(std::u16string name, const GURL& url);
  bool UpdateSite(const std::string& id, std::u16string name, const GURL& url);
  bool RemoveSite(const std::string& id);
  bool MoveSite(const std::string& id, size_t new_index);
  void UpdateLastUrl(const std::string& id, const GURL& url);
  void SetMuted(const std::string& id, bool muted);
  // `icon` is a data: URL, or empty to fall back to the favicon.
  bool SetIcon(const std::string& id, std::string icon);

  base::CallbackListSubscription AddChangedCallback(
      base::RepeatingClosure callback);

 private:
  void Load();
  void Save(bool notify = true);
  // Drops the old bundled ChatGPT/Gemini/Gmail/Discord pins if they are still
  // in this profile from when the rail used to seed them.
  void RemoveLegacyDefaultSites();
  void OnPrefChanged();

  const raw_ptr<PrefService> prefs_;
  std::vector<FluxSite> sites_;
  PrefChangeRegistrar pref_change_registrar_;
  base::RepeatingClosureList changed_callbacks_;
  bool saving_ = false;
};

}  // namespace flux_sidebar

#endif  // CHROME_BROWSER_UI_VIEWS_FLUX_SIDEBAR_FLUX_SIDEBAR_MODEL_H_
