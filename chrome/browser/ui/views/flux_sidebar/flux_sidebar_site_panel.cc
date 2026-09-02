// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/flux_sidebar/flux_sidebar_site_panel.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/input/native_web_keyboard_event.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/mojom/window_features/window_features.mojom.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/webview/unhandled_keyboard_event_handler.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/box_layout.h"
#include "url/gurl.h"

namespace flux_sidebar {
namespace {

constexpr base::TimeDelta kWarmLifetime = base::Minutes(3);

std::unique_ptr<views::LabelButton> MakeButton(
    std::u16string text,
    std::u16string tooltip,
    views::Button::PressedCallback callback) {
  auto button =
      std::make_unique<views::LabelButton>(std::move(callback), text);
  button->SetTooltipText(tooltip);
  button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  return button;
}

bool IsNewSurfaceDisposition(WindowOpenDisposition disposition) {
  switch (disposition) {
    case WindowOpenDisposition::NEW_FOREGROUND_TAB:
    case WindowOpenDisposition::NEW_BACKGROUND_TAB:
    case WindowOpenDisposition::NEW_POPUP:
    case WindowOpenDisposition::NEW_WINDOW:
    case WindowOpenDisposition::OFF_THE_RECORD:
      return true;
    default:
      return false;
  }
}

}  // namespace

class FluxSiteWebView : public views::WebView {
  METADATA_HEADER(FluxSiteWebView, views::WebView)

 public:
  FluxSiteWebView(Profile* profile, BrowserWindowInterface* browser)
      : views::WebView(profile), browser_(browser) {}

  content::WebContents* OpenURLFromTab(
      content::WebContents* source,
      const content::OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)>
          navigation_handle_callback) override {
    if (IsNewSurfaceDisposition(params.disposition)) {
      if (!params.url.SchemeIsHTTPOrHTTPS()) {
        return nullptr;
      }
      return browser_->OpenURL(params, std::move(navigation_handle_callback));
    }

    content::NavigationController::LoadURLParams load(params.url);
    load.referrer = params.referrer;
    load.transition_type = params.transition;
    load.is_renderer_initiated = params.is_renderer_initiated;
    load.initiator_origin = params.initiator_origin;
    source->GetController().LoadURLWithParams(load);
    return source;
  }

  content::WebContents* AddNewContents(
      content::WebContents* source,
      std::unique_ptr<content::WebContents> new_contents,
      const GURL& target_url,
      WindowOpenDisposition disposition,
      const blink::mojom::WindowFeatures& window_features,
      bool user_gesture,
      bool* was_blocked) override {
    if (!target_url.SchemeIsHTTPOrHTTPS() && !target_url.is_empty()) {
      if (was_blocked) {
        *was_blocked = true;
      }
      return nullptr;
    }
    chrome::AddWebContents(browser_, source, std::move(new_contents),
                           target_url, disposition, window_features);
    return nullptr;
  }

  bool HandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override {
    return unhandled_keyboard_event_handler_.HandleKeyboardEvent(
        event, GetFocusManager());
  }

 private:
  const raw_ptr<BrowserWindowInterface> browser_;
  views::UnhandledKeyboardEventHandler unhandled_keyboard_event_handler_;
};

FluxSitePanel::FluxSitePanel(BrowserView* browser_view,
                             const FluxSite& site,
                             UrlChangedCallback url_changed)
    : browser_view_(browser_view),
      site_id_(site.id),
      fallback_title_(site.name),
      url_changed_(std::move(url_changed)) {
  auto* root_layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  root_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto toolbar = std::make_unique<views::View>();
  auto* toolbar_layout =
      toolbar->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(4, 6),
          3));
  toolbar_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto reload = MakeButton(
      u"↻", u"Reload",
      base::BindRepeating(&FluxSitePanel::Reload, base::Unretained(this)));
  reload->SetPreferredSize(gfx::Size(28, 28));
  toolbar->AddChildView(std::move(reload));

  auto title = std::make_unique<views::Label>(site.name);
  title->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  title->SetElideBehavior(gfx::ELIDE_TAIL);
  title_label_ = toolbar->AddChildView(std::move(title));
  toolbar_layout->SetFlexForView(title_label_, 1);

  auto trailing_spacer = std::make_unique<views::View>();
  trailing_spacer->SetPreferredSize(gfx::Size(28, 28));
  toolbar->AddChildView(std::move(trailing_spacer));
  AddChildView(std::move(toolbar));

  auto web_view = std::make_unique<FluxSiteWebView>(
      browser_view_->GetProfile(), browser_view_->browser());
  web_view_ = AddChildView(std::move(web_view));
  root_layout->SetFlexForView(web_view_, 1);
  web_view_->LoadInitialURL(site.url);
  Observe(web_view_->web_contents());
  if (content::WebContents* contents = web_contents()) {
    contents->SetAudioMuted(site.muted);
  }
  // The role must be set before any name: AXNodeData::SetName() CHECKs for a
  // known role so it can pick the default NameFrom. UpdateTitle() then names
  // the panel after the live page title, falling back to `site.name`.
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  UpdateTitle();
}

FluxSitePanel::~FluxSitePanel() {
  Observe(nullptr);
}

void FluxSitePanel::StartDiscardTimer(DiscardCallback callback) {
  if (discard_timer_.IsRunning()) {
    return;
  }
  discard_callback_ = std::move(callback);
  discard_timer_.Start(
      FROM_HERE, kWarmLifetime,
      base::BindOnce(&FluxSitePanel::Discard, base::Unretained(this)));
}

void FluxSitePanel::StopDiscardTimer() {
  discard_timer_.Stop();
  discard_callback_.Reset();
}

void FluxSitePanel::ApplySiteState(const FluxSite& site) {
  content::WebContents* contents = web_contents();
  if (!contents) {
    return;
  }
  if (contents->IsAudioMuted() != site.muted) {
    contents->SetAudioMuted(site.muted);
  }
}

void FluxSitePanel::DidFinishNavigation(content::NavigationHandle* handle) {
  if (handle->HasCommitted() && handle->IsInPrimaryMainFrame() &&
      handle->GetURL().SchemeIsHTTPOrHTTPS()) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&FluxSitePanel::NotifyUrlChanged,
                       weak_ptr_factory_.GetWeakPtr(), handle->GetURL()));
  }
  ScheduleUpdateTitle();
}

void FluxSitePanel::TitleWasSet(content::NavigationEntry* entry) {
  ScheduleUpdateTitle();
}

void FluxSitePanel::WebContentsDestroyed() {
  Observe(nullptr);
}

void FluxSitePanel::Reload() {
  content::WebContents* contents = web_contents();
  if (!contents) {
    return;
  }
  contents->GetController().Reload(content::ReloadType::NORMAL, true);
}

void FluxSitePanel::Discard() {
  if (discard_callback_) {
    std::move(discard_callback_).Run();
  }
}

void FluxSitePanel::ScheduleUpdateTitle() {
  if (title_update_pending_) {
    return;
  }
  title_update_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&FluxSitePanel::UpdateTitle,
                                weak_ptr_factory_.GetWeakPtr()));
}

void FluxSitePanel::UpdateTitle() {
  title_update_pending_ = false;
  if (!title_label_) {
    return;
  }
  content::WebContents* contents = web_contents();
  std::u16string title = contents ? contents->GetTitle() : std::u16string();
  if (title.empty()) {
    title = fallback_title_;
  }
  title_label_->SetText(title);
  title_label_->SetTooltipText(title);
  GetViewAccessibility().SetName(title);
}

void FluxSitePanel::NotifyUrlChanged(const GURL& url) {
  if (url_changed_) {
    url_changed_.Run(url);
  }
}

BEGIN_METADATA(FluxSiteWebView)
END_METADATA

BEGIN_METADATA(FluxSitePanel)
END_METADATA

}  // namespace flux_sidebar
