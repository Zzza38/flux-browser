// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FLUX_SIDEBAR_FLUX_SIDEBAR_SITE_PANEL_H_
#define CHROME_BROWSER_UI_VIEWS_FLUX_SIDEBAR_FLUX_SIDEBAR_SITE_PANEL_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/views/flux_sidebar/flux_sidebar_model.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"
#include "url/gurl.h"

class BrowserView;

namespace views {
class Label;
class WebView;
}  // namespace views

namespace flux_sidebar {

// Hosts one pinned website as a profile-backed WebContents. Inactive panels
// stay warm for three minutes, then the renderer is destroyed while the last
// URL is kept in the model.
class FluxSitePanel : public views::View, public content::WebContentsObserver {
  METADATA_HEADER(FluxSitePanel, views::View)

 public:
  using UrlChangedCallback = base::RepeatingCallback<void(const GURL&)>;
  using DiscardCallback = base::OnceClosure;

  FluxSitePanel(BrowserView* browser_view,
                const FluxSite& site,
                UrlChangedCallback url_changed);
  FluxSitePanel(const FluxSitePanel&) = delete;
  FluxSitePanel& operator=(const FluxSitePanel&) = delete;
  ~FluxSitePanel() override;

  const std::string& site_id() const { return site_id_; }

  // Starts the three-minute warm timer if it is not already running.
  void StartDiscardTimer(DiscardCallback callback);
  void StopDiscardTimer();

  void ApplySiteState(const FluxSite& site);

  // content::WebContentsObserver:
  void DidFinishNavigation(content::NavigationHandle* handle) override;
  void TitleWasSet(content::NavigationEntry* entry) override;
  void WebContentsDestroyed() override;

 private:
  void Reload();
  void Discard();
  void ScheduleUpdateTitle();
  void UpdateTitle();
  void NotifyUrlChanged(const GURL& url);

  const raw_ptr<BrowserView> browser_view_;
  const std::string site_id_;
  const std::u16string fallback_title_;
  raw_ptr<views::WebView> web_view_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  UrlChangedCallback url_changed_;
  DiscardCallback discard_callback_;
  base::OneShotTimer discard_timer_;
  bool title_update_pending_ = false;
  base::WeakPtrFactory<FluxSitePanel> weak_ptr_factory_{this};
};

}  // namespace flux_sidebar

#endif  // CHROME_BROWSER_UI_VIEWS_FLUX_SIDEBAR_FLUX_SIDEBAR_SITE_PANEL_H_
