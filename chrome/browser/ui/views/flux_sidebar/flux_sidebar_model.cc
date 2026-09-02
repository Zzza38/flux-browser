// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/flux_sidebar/flux_sidebar_model.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/common/flux_sidebar_pref_names.h"
#include "components/prefs/pref_service.h"

namespace flux_sidebar {
namespace {

constexpr char kIdKey[] = "id";
constexpr char kNameKey[] = "name";
constexpr char kUrlKey[] = "url";
constexpr char kMarkKey[] = "mark";
constexpr char kColorKey[] = "color";
constexpr char kMutedKey[] = "muted";
constexpr char kIconKey[] = "icon";

// Ids used when the rail used to ship with bundled pins. User-added sites get
// UUIDs, so these only match the old defaults.
constexpr std::array<std::string_view, 4> kLegacyDefaultSiteIds{
    "chatgpt", "gemini", "gmail", "discord"};

bool IsAllowedUrl(const GURL& url) {
  return url.is_valid() && (url.SchemeIsHTTPOrHTTPS());
}

std::u16string MarkForName(const std::u16string& name) {
  if (name.empty()) {
    return u"?";
  }
  return std::u16string(1, base::ToUpperASCII(name.front()));
}

}  // namespace

FluxSidebarModel::FluxSidebarModel(PrefService* prefs) : prefs_(prefs) {
  CHECK(prefs_);
  Load();
  RemoveLegacyDefaultSites();
  pref_change_registrar_.Init(prefs_);
  pref_change_registrar_.Add(
      prefs::kFluxSidebarSites,
      base::BindRepeating(&FluxSidebarModel::OnPrefChanged,
                          base::Unretained(this)));
}

FluxSidebarModel::~FluxSidebarModel() = default;

const FluxSite* FluxSidebarModel::FindSite(const std::string& id) const {
  auto it = std::ranges::find(sites_, id, &FluxSite::id);
  return it == sites_.end() ? nullptr : &*it;
}

bool FluxSidebarModel::AddSite(std::u16string name, const GURL& url) {
  if (name.empty() || !IsAllowedUrl(url)) {
    return false;
  }
  sites_.push_back({base::Uuid::GenerateRandomV4().AsLowercaseString(),
                    std::move(name), url, std::u16string(), "#a8ff78",
                    /*muted=*/false});
  sites_.back().mark = MarkForName(sites_.back().name);
  Save();
  return true;
}

bool FluxSidebarModel::UpdateSite(const std::string& id,
                                  std::u16string name,
                                  const GURL& url) {
  FluxSite* site = const_cast<FluxSite*>(FindSite(id));
  if (!site || name.empty() || !IsAllowedUrl(url)) {
    return false;
  }
  site->name = std::move(name);
  site->url = url;
  site->mark = MarkForName(site->name);
  Save();
  return true;
}

bool FluxSidebarModel::RemoveSite(const std::string& id) {
  const auto old_size = sites_.size();
  std::erase_if(sites_, [&](const FluxSite& site) { return site.id == id; });
  if (sites_.size() == old_size) {
    return false;
  }
  Save();
  return true;
}

bool FluxSidebarModel::MoveSite(const std::string& id, size_t new_index) {
  auto it = std::ranges::find(sites_, id, &FluxSite::id);
  if (it == sites_.end() || new_index >= sites_.size()) {
    return false;
  }
  FluxSite site = std::move(*it);
  sites_.erase(it);
  sites_.insert(sites_.begin() + new_index, std::move(site));
  Save();
  return true;
}

void FluxSidebarModel::UpdateLastUrl(const std::string& id, const GURL& url) {
  FluxSite* site = const_cast<FluxSite*>(FindSite(id));
  if (!site || !IsAllowedUrl(url) || site->url == url) {
    return;
  }
  site->url = url;
  // Persist only. Notifying rebuilds the rail, and this is called from
  // DidFinishNavigation while WebContents observers are still iterating.
  Save(/*notify=*/false);
}

void FluxSidebarModel::SetMuted(const std::string& id, bool muted) {
  FluxSite* site = const_cast<FluxSite*>(FindSite(id));
  if (!site || site->muted == muted) {
    return;
  }
  site->muted = muted;
  Save();
}

bool FluxSidebarModel::SetIcon(const std::string& id, std::string icon) {
  FluxSite* site = const_cast<FluxSite*>(FindSite(id));
  if (!site || site->icon == icon) {
    return false;
  }
  site->icon = std::move(icon);
  Save();
  return true;
}

base::CallbackListSubscription FluxSidebarModel::AddChangedCallback(
    base::RepeatingClosure callback) {
  return changed_callbacks_.Add(std::move(callback));
}

void FluxSidebarModel::Load() {
  std::vector<FluxSite> loaded;
  for (const base::Value& value : prefs_->GetList(prefs::kFluxSidebarSites)) {
    const base::DictValue* dict = value.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString(kIdKey);
    const std::string* name = dict->FindString(kNameKey);
    const std::string* url_value = dict->FindString(kUrlKey);
    const std::string* mark = dict->FindString(kMarkKey);
    const std::string* color = dict->FindString(kColorKey);
    const std::string* icon = dict->FindString(kIconKey);
    GURL url(url_value ? *url_value : std::string());
    if (!id || !name || !IsAllowedUrl(url)) {
      continue;
    }
    loaded.push_back({*id, base::UTF8ToUTF16(*name), url,
                      mark ? base::UTF8ToUTF16(*mark) : u"?",
                      color ? *color : "#a8ff78",
                      dict->FindBool(kMutedKey).value_or(false),
                      icon ? *icon : std::string()});
  }
  sites_ = std::move(loaded);
}

void FluxSidebarModel::Save(bool notify) {
  base::ListValue list;
  for (const FluxSite& site : sites_) {
    base::DictValue dict;
    dict.Set(kIdKey, site.id);
    dict.Set(kNameKey, base::UTF16ToUTF8(site.name));
    dict.Set(kUrlKey, site.url.spec());
    dict.Set(kMarkKey, base::UTF16ToUTF8(site.mark));
    dict.Set(kColorKey, site.color);
    dict.Set(kMutedKey, site.muted);
    dict.Set(kIconKey, site.icon);
    list.Append(std::move(dict));
  }
  saving_ = true;
  prefs_->SetList(prefs::kFluxSidebarSites, std::move(list));
  saving_ = false;
  if (notify) {
    changed_callbacks_.Notify();
  }
}

void FluxSidebarModel::RemoveLegacyDefaultSites() {
  const auto old_size = sites_.size();
  std::erase_if(sites_, [](const FluxSite& site) {
    return std::ranges::find(kLegacyDefaultSiteIds, site.id) !=
           kLegacyDefaultSiteIds.end();
  });
  if (sites_.size() != old_size) {
    Save(/*notify=*/false);
  }
}

void FluxSidebarModel::OnPrefChanged() {
  if (saving_) {
    return;
  }
  Load();
  RemoveLegacyDefaultSites();
  changed_callbacks_.Notify();
}

}  // namespace flux_sidebar
