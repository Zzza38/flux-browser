// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FLUX_SIDEBAR_FLUX_SIDEBAR_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_FLUX_SIDEBAR_FLUX_SIDEBAR_VIEW_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "chrome/browser/ui/views/flux_sidebar/flux_sidebar_model.h"
#include "components/favicon_base/favicon_types.h"
#include "components/prefs/pref_change_registrar.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/select_file_policy.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/resize_area_delegate.h"
#include "ui/views/view.h"

class BrowserView;
class SkBitmap;

namespace network {
class SimpleURLLoader;
}  // namespace network

namespace views {
class BoxLayout;
class ImageButton;
class LabelButton;
class MenuRunner;
class ResizeArea;
}  // namespace views

namespace flux_sidebar {

class FluxSitePanel;

// Native left rail and profile-backed website host. BrowserView lays this out
// at full window height, so opening a panel shrinks the rest of the browser.
class FluxSidebarView : public views::View,
                        public views::ResizeAreaDelegate,
                        public views::ContextMenuController,
                        public ui::SimpleMenuModel::Delegate,
                        public ui::SelectFileDialog::Listener {
  METADATA_HEADER(FluxSidebarView, views::View)

 public:
  static constexpr int kRailWidth = 52;
  // Unpainted hit target that hangs off the open panel into the browser.
  static constexpr int kResizeHandleWidth = 20;

  explicit FluxSidebarView(BrowserView* browser_view);
  FluxSidebarView(const FluxSidebarView&) = delete;
  FluxSidebarView& operator=(const FluxSidebarView&) = delete;
  ~FluxSidebarView() override;

  int GetPreferredWidth(int available_width) const;
  // Zero when the panel is closed, so the handle does not exist without a
  // popout.
  int GetResizeHandleWidth() const;
  bool IsPanelOpen() const { return !active_site_ids_.empty(); }

  // views::ResizeAreaDelegate:
  void OnResize(int resize_amount, bool done_resizing) override;

  // views::View:
  void Layout(PassKey) override;
  void OnThemeChanged() override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;

  // ui::SelectFileDialog::Listener:
  void FileSelected(const ui::SelectedFileInfo& file, int index) override;
  void FileSelectionCanceled() override;

 private:
  void RebuildRail();
  void OnModelChanged();
  void ActivateSite(const std::string& id);
  void CloseSite(const std::string& id);
  void ClosePanel();
  void RemoveSite(const std::string& id);
  void FinishRemoveSite(const std::string& id);
  void ShowAddSiteDialog();
  void AddSite(std::u16string name, GURL url);
  void UpdateVisiblePanels();
  void UpdatePanelLayout();
  void DiscardSite(const std::string& id);
  FluxSitePanel* FindSession(const std::string& id) const;
  FluxSitePanel* GetOrCreateSession(const FluxSite& site);
  bool IsActive(const std::string& id) const;
  void InvalidateBrowserLayout();
  void UpdateLogoColor();
  void OnEnabledChanged();
  // Requests favicons for any site that does not have one cached yet.
  void LoadFavicons();
  void OnFaviconLoaded(const std::string& id,
                       const favicon_base::FaviconImageResult& result);
  void FetchFavicon(const std::string& id, const GURL& page_url);
  void DownloadFaviconImage(const std::string& id, const GURL& icon_url);
  void OnFaviconBytesRead(const std::string& id,
                          std::optional<std::string> bytes);
  void OnDownloadedFaviconDecoded(const std::string& id,
                                  const SkBitmap& bitmap);
  void FetchFaviconFromPage(const std::string& id);
  void OnFaviconPageRead(const std::string& id,
                         std::optional<std::string> html);

  // Custom icon pipeline. A file or a remote image both end up as raw bytes,
  // get decoded out-of-process, and are re-encoded into the site's pref as a
  // PNG data: URL.
  void ChooseIconFile();
  void ShowIconUrlDialog();
  void FetchIconFromUrl(const GURL& url);
  // Shared completion for both sources: SimpleURLLoader's body callback and
  // the file read both hand back an optional<string>.
  void OnIconBytesRead(std::optional<std::string> bytes);
  void DecodeIconBytes(const std::string& bytes);
  void OnIconDecoded(const SkBitmap& bitmap);
  // Decoded icon for `site`, or a null image when it has no custom icon.
  gfx::ImageSkia GetCustomIcon(const FluxSite& site);

  const raw_ptr<BrowserView> browser_view_;
  FluxSidebarModel model_;
  base::CallbackListSubscription model_subscription_;

  raw_ptr<views::View> rail_ = nullptr;
  raw_ptr<views::ImageButton> logo_button_ = nullptr;
  raw_ptr<views::View> site_buttons_ = nullptr;
  raw_ptr<views::View> panel_host_ = nullptr;
  raw_ptr<views::BoxLayout> panel_layout_ = nullptr;
  raw_ptr<views::ResizeArea> resize_area_ = nullptr;

  std::vector<std::string> active_site_ids_;
  std::vector<raw_ptr<FluxSitePanel>> sessions_;
  // Site id -> favicon. Populated asynchronously; sites missing an entry fall
  // back to their letter mark.
  std::map<std::string, gfx::Image> favicons_;
  base::CancelableTaskTracker favicon_task_tracker_;
  std::set<std::string> favicon_download_attempts_;
  std::set<std::string> favicon_page_attempts_;
  std::map<std::string, std::unique_ptr<network::SimpleURLLoader>>
      favicon_loaders_;
  PrefChangeRegistrar pref_change_registrar_;

  std::unique_ptr<ui::SimpleMenuModel> context_menu_model_;
  std::unique_ptr<views::MenuRunner> context_menu_runner_;
  scoped_refptr<ui::SelectFileDialog> select_file_dialog_;
  std::unique_ptr<network::SimpleURLLoader> icon_loader_;
  // Maps each rail button back to its site so the context menu knows its
  // target. Rebuilt with the rail.
  std::map<const views::View*, std::string> button_site_ids_;
  // Site the context menu was opened on, and the one an in-flight icon
  // fetch belongs to. Only one icon can be picked at a time.
  std::string context_menu_site_id_;
  std::string pending_icon_site_id_;
  // Decoded custom icons keyed by their data: URL, so changing an icon
  // naturally misses the cache.
  std::map<std::string, gfx::ImageSkia> decoded_icons_;
  int panel_width_ = 0;
  int starting_panel_width_ = -1;
  bool split_ = false;
  bool split_vertical_ = false;

  base::WeakPtrFactory<FluxSidebarView> weak_ptr_factory_{this};
};

}  // namespace flux_sidebar

#endif  // CHROME_BROWSER_UI_VIEWS_FLUX_SIDEBAR_FLUX_SIDEBAR_VIEW_H_
