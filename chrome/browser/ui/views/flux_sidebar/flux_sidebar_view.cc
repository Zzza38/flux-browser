// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/flux_sidebar/flux_sidebar_view.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/flux_sidebar/flux_sidebar_site_panel.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/flux_sidebar_pref_names.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/embedder_support/user_agent_utils.h"
#include "components/favicon/core/favicon_service.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/color_parser.h"
#include "net/http/http_request_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/data_decoder/public/cpp/decode_image.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "skia/ext/image_operations.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/shell_dialogs/selected_file_info.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/resize_area.h"
#include "ui/views/layout/box_layout.h"

namespace flux_sidebar {
namespace {

constexpr int kPanelDefaultWidthNumerator = 3;
constexpr int kPanelDefaultWidthDenominator = 8;
constexpr int kPanelMinimumPercent = 10;
constexpr int kRailButtonSize = 40;
constexpr int kRailButtonRadius = 9;
constexpr int kLogoIconSize = 28;
// The icon sits on a neutral "platform" rather than filling it, so it is
// deliberately smaller than kRailButtonSize.
constexpr int kRailIconSize = 22;
// Ring drawn around the platform of the active site. Reserved as an empty
// border on inactive sites so every icon stays centred on its platform.
constexpr int kSelectionRingThickness = 2;

std::unique_ptr<views::LabelButton> MakeButton(
    std::u16string text,
    std::u16string tooltip,
    views::Button::PressedCallback callback,
    gfx::Size preferred_size = gfx::Size()) {
  auto button = std::make_unique<views::LabelButton>(std::move(callback), text);
  button->SetTooltipText(tooltip);
  if (!preferred_size.IsEmpty()) {
    button->SetPreferredSize(preferred_size);
  }
  button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  return button;
}

std::unique_ptr<views::ImageButton> MakeLogoButton(
    views::Button::PressedCallback callback) {
  auto button = views::CreateVectorImageButton(std::move(callback));
  button->SetPreferredSize(gfx::Size(kRailButtonSize, kRailButtonSize));
  button->SetTooltipText(u"Close Flux panel");
  button->GetViewAccessibility().SetName(u"Close Flux panel");
  return button;
}

DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kAddSiteNameFieldId);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kAddSiteUrlFieldId);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kIconUrlFieldId);

// Context menu commands for a site tile.
enum IconCommand {
  kChooseIconFile = 1,
  kUseIconUrl,
  kResetIcon,
};

// Custom icons are normalised to this square size before being stored, so a
// huge source image cannot bloat the (synced) pref.
constexpr int kCustomIconSizePx = 64;
constexpr size_t kMaxIconSourceBytes = 4 * 1024 * 1024;
constexpr size_t kMaxFaviconBytes = 512 * 1024;
constexpr size_t kMaxFaviconPageBytes = 1024 * 1024;

std::optional<std::string> FindHtmlAttribute(std::string_view tag,
                                             std::string_view name) {
  const std::string lower = base::ToLowerASCII(tag);
  size_t position = 0;
  while ((position = lower.find(name, position)) != std::string::npos) {
    const size_t after_name = position + name.size();
    if ((position > 0 && !base::IsAsciiWhitespace(lower[position - 1])) ||
        (after_name < lower.size() &&
         !base::IsAsciiWhitespace(lower[after_name]) &&
         lower[after_name] != '=')) {
      position = after_name;
      continue;
    }
    size_t equals = after_name;
    while (equals < lower.size() && base::IsAsciiWhitespace(lower[equals])) {
      ++equals;
    }
    if (equals >= lower.size() || lower[equals] != '=') {
      position = after_name;
      continue;
    }
    size_t value_start = equals + 1;
    while (value_start < tag.size() &&
           base::IsAsciiWhitespace(tag[value_start])) {
      ++value_start;
    }
    if (value_start >= tag.size()) {
      return std::nullopt;
    }
    const char quote = tag[value_start];
    if (quote == '\'' || quote == '"') {
      const size_t value_end = tag.find(quote, value_start + 1);
      if (value_end == std::string_view::npos) {
        return std::nullopt;
      }
      return std::string(
          tag.substr(value_start + 1, value_end - value_start - 1));
    }
    const size_t value_end = tag.find_first_of(" \t\r\n>", value_start);
    return std::string(tag.substr(value_start, value_end - value_start));
  }
  return std::nullopt;
}

std::optional<GURL> FindDeclaredFavicon(std::string_view html,
                                        const GURL& page_url) {
  const std::string lower = base::ToLowerASCII(html);
  std::optional<GURL> svg_fallback;
  size_t position = 0;
  while ((position = lower.find("<link", position)) != std::string::npos) {
    const size_t end = lower.find('>', position + 5);
    if (end == std::string::npos) {
      break;
    }
    const std::string_view tag = html.substr(position, end - position + 1);
    const std::optional<std::string> rel = FindHtmlAttribute(tag, "rel");
    const std::optional<std::string> href = FindHtmlAttribute(tag, "href");
    position = end + 1;
    if (!rel || !href ||
        base::ToLowerASCII(*rel).find("icon") == std::string::npos) {
      continue;
    }
    const GURL candidate = page_url.Resolve(*href);
    if (!candidate.SchemeIsHTTPOrHTTPS()) {
      continue;
    }
    if (base::EndsWith(candidate.path(), ".svg",
                       base::CompareCase::INSENSITIVE_ASCII)) {
      if (!svg_fallback) {
        svg_fallback = candidate;
      }
      continue;
    }
    return candidate;
  }
  return svg_fallback;
}
constexpr char kIconDataUrlPrefix[] = "data:image/png;base64,";

std::optional<std::string> ReadFileBytes(const base::FilePath& path) {
  std::string contents;
  if (!base::ReadFileToStringWithMaxSize(path, &contents,
                                         kMaxIconSourceBytes)) {
    return std::nullopt;
  }
  return contents;
}

}  // namespace

FluxSidebarView::FluxSidebarView(BrowserView* browser_view)
    : browser_view_(browser_view),
      model_(browser_view->GetProfile()->GetPrefs()),
      panel_width_(browser_view->GetProfile()->GetPrefs()->GetInteger(
          prefs::kFluxSidebarWidth)),
      split_vertical_(browser_view->GetProfile()->GetPrefs()->GetBoolean(
          prefs::kFluxSidebarSplitVertical)) {
  rail_ = AddChildView(std::make_unique<views::View>());
  rail_->SetPreferredSize(gfx::Size(kRailWidth, 0));
  rail_->SetBackground(views::CreateSolidBackground(ui::kColorSysSurface2));
  rail_->SetBorder(views::CreateSolidSidedBorder(gfx::Insets().set_right(1),
                                                 ui::kColorSysDivider));

  panel_host_ = AddChildView(std::make_unique<views::View>());
  panel_layout_ =
      panel_host_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal));
  panel_layout_->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  panel_host_->SetVisible(false);

  resize_area_ = AddChildView(std::make_unique<views::ResizeArea>(this));
  resize_area_->SetPreferredSize(gfx::Size(kResizeHandleWidth, 0));
  resize_area_->SetPaintToLayer();
  resize_area_->layer()->SetFillsBoundsOpaquely(false);
  resize_area_->SetVisible(false);

  model_subscription_ = model_.AddChangedCallback(base::BindRepeating(
      &FluxSidebarView::OnModelChanged, base::Unretained(this)));
  pref_change_registrar_.Init(browser_view_->GetProfile()->GetPrefs());
  pref_change_registrar_.Add(
      prefs::kFluxSidebarEnabled,
      base::BindRepeating(&FluxSidebarView::OnEnabledChanged,
                          base::Unretained(this)));
  OnEnabledChanged();
  RebuildRail();
  GetViewAccessibility().SetRole(ax::mojom::Role::kComplementary);
  GetViewAccessibility().SetName(u"Flux website sidebar");
}

FluxSidebarView::~FluxSidebarView() = default;

int FluxSidebarView::GetPreferredWidth(int available_width) const {
  if (!browser_view_->GetProfile()->GetPrefs()->GetBoolean(
          prefs::kFluxSidebarEnabled)) {
    return 0;
  }
  if (!IsPanelOpen()) {
    return kRailWidth;
  }
  const int minimum = std::max(1, available_width * kPanelMinimumPercent / 100);
  const int maximum = std::max(minimum, available_width - kRailWidth);
  const int desired = panel_width_ > 0
                          ? panel_width_
                          : available_width * kPanelDefaultWidthNumerator /
                                kPanelDefaultWidthDenominator;
  return kRailWidth + std::clamp(desired, minimum, maximum);
}

int FluxSidebarView::GetResizeHandleWidth() const {
  return IsPanelOpen() ? kResizeHandleWidth : 0;
}

void FluxSidebarView::OnResize(int resize_amount, bool done_resizing) {
  if (starting_panel_width_ < 0) {
    starting_panel_width_ =
        std::max(1, width() - kRailWidth - GetResizeHandleWidth());
  }
  const int available = parent() ? parent()->width() : width();
  const int minimum = std::max(1, available * kPanelMinimumPercent / 100);
  const int maximum = std::max(minimum, available - kRailWidth);
  panel_width_ =
      std::clamp(starting_panel_width_ + resize_amount, minimum, maximum);
  if (done_resizing) {
    starting_panel_width_ = -1;
    browser_view_->GetProfile()->GetPrefs()->SetInteger(
        prefs::kFluxSidebarWidth, panel_width_);
  }
  InvalidateBrowserLayout();
}

void FluxSidebarView::RebuildRail() {
  logo_button_ = nullptr;
  site_buttons_ = nullptr;
  button_site_ids_.clear();
  rail_->RemoveAllChildViews();
  auto* layout = rail_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(8, 6), 6));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  logo_button_ = rail_->AddChildView(MakeLogoButton(base::BindRepeating(
      &FluxSidebarView::ClosePanel, base::Unretained(this))));
  UpdateLogoColor();

  auto upper_spacer = std::make_unique<views::View>();
  views::View* upper_spacer_ptr =
      rail_->AddChildView(std::move(upper_spacer));
  layout->SetFlexForView(upper_spacer_ptr, 1);

  site_buttons_ = rail_->AddChildView(std::make_unique<views::View>());
  auto* sites_layout =
      site_buttons_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), 6));
  sites_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  for (const FluxSite& site : model_.sites()) {
    auto button =
        MakeButton(site.mark, site.name,
                   base::BindRepeating(&FluxSidebarView::ActivateSite,
                                       base::Unretained(this), site.id),
                   gfx::Size(kRailButtonSize, kRailButtonSize));
    button->SetAccessibleName(site.name);
    button->set_context_menu_controller(this);

    // Neutral platform behind the icon, one step up from the rail's
    // kColorSysSurface2 so it reads as a raised tile in both light and dark.
    button->SetBackground(views::CreateRoundedRectBackground(
        ui::kColorSysSurface3, kRailButtonRadius, kSelectionRingThickness));
    // Identical insets either way, so switching sites never shifts the icon;
    // only the ring's colour changes.
    button->SetBorder(
        IsActive(site.id)
            ? views::CreateRoundedRectBorder(kSelectionRingThickness,
                                             kRailButtonRadius,
                                             ui::kColorSysPrimary)
            : views::CreateEmptyBorder(gfx::Insets(kSelectionRingThickness)));

    const gfx::ImageSkia custom_icon = GetCustomIcon(site);
    const auto favicon = favicons_.find(site.id);
    if (!custom_icon.isNull()) {
      button->SetImageModel(
          views::Button::STATE_NORMAL,
          ui::ImageModel::FromImageSkia(
              gfx::ImageSkiaOperations::CreateResizedImage(
                  custom_icon, skia::ImageOperations::RESIZE_BEST,
                  gfx::Size(kRailIconSize, kRailIconSize))));
      button->SetText(std::u16string());
    } else if (favicon != favicons_.end() && !favicon->second.IsEmpty()) {
      // Favicons arrive at their natural size (usually 16dip); scale them to
      // the platform's icon size so every tile matches.
      button->SetImageModel(views::Button::STATE_NORMAL,
                            ui::ImageModel::FromImageSkia(
                                gfx::ImageSkiaOperations::CreateResizedImage(
                                    favicon->second.AsImageSkia(),
                                    skia::ImageOperations::RESIZE_BEST,
                                    gfx::Size(kRailIconSize, kRailIconSize))));
      button->SetText(std::u16string());
    } else {
      // No favicon yet: keep the letter mark, tinted with the site's colour so
      // tiles stay distinguishable on the neutral platform.
      SkColor color = SK_ColorGRAY;
      content::ParseHexColorString(site.color, &color);
      button->SetEnabledTextColors(color);
    }
    button_site_ids_[site_buttons_->AddChildView(std::move(button))] = site.id;
  }

  rail_->AddChildView(
      MakeButton(u"+", u"Add website",
                 base::BindRepeating(&FluxSidebarView::ShowAddSiteDialog,
                                     base::Unretained(this)),
                 gfx::Size(kRailButtonSize, kRailButtonSize)));

  auto lower_spacer = std::make_unique<views::View>();
  views::View* lower_spacer_ptr =
      rail_->AddChildView(std::move(lower_spacer));
  layout->SetFlexForView(lower_spacer_ptr, 1);

  rail_->InvalidateLayout();
}

void FluxSidebarView::OnModelChanged() {
  std::erase_if(active_site_ids_,
                [this](const std::string& id) { return !model_.FindSite(id); });
  for (auto it = sessions_.begin(); it != sessions_.end();) {
    if (!model_.FindSite((*it)->site_id())) {
      panel_host_->RemoveChildViewT(*it);
      it = sessions_.erase(it);
    } else {
      ++it;
    }
  }
  if (active_site_ids_.size() < 2) {
    split_ = false;
  }
  LoadFavicons();
  RebuildRail();
  UpdateVisiblePanels();
}

void FluxSidebarView::ActivateSite(const std::string& id) {
  if (!model_.FindSite(id)) {
    return;
  }
  if (!split_ && active_site_ids_.size() == 1 &&
      active_site_ids_.front() == id) {
    active_site_ids_.clear();
  } else if (split_) {
    std::erase(active_site_ids_, id);
    active_site_ids_.insert(active_site_ids_.begin(), id);
    if (active_site_ids_.size() < 2) {
      for (const FluxSite& site : model_.sites()) {
        if (site.id != id) {
          active_site_ids_.push_back(site.id);
          break;
        }
      }
    }
    if (active_site_ids_.size() > 2) {
      active_site_ids_.resize(2);
    }
  } else {
    active_site_ids_ = {id};
  }
  RebuildRail();
  UpdateVisiblePanels();
}

void FluxSidebarView::CloseSite(const std::string& id) {
  std::erase(active_site_ids_, id);
  if (active_site_ids_.size() < 2) {
    split_ = false;
  }
  RebuildRail();
  UpdateVisiblePanels();
}

void FluxSidebarView::ClosePanel() {
  if (active_site_ids_.empty()) {
    return;
  }
  active_site_ids_.clear();
  split_ = false;
  RebuildRail();
  UpdateVisiblePanels();
}

void FluxSidebarView::RemoveSite(const std::string& id) {
  CloseSite(id);
  // The request comes from a toolbar owned by the session being removed.
  // Defer model mutation so the button callback can return before that view is
  // destroyed by OnModelChanged().
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&FluxSidebarView::FinishRemoveSite,
                                weak_ptr_factory_.GetWeakPtr(), id));
}

void FluxSidebarView::FinishRemoveSite(const std::string& id) {
  model_.RemoveSite(id);
}

void FluxSidebarView::ShowAddSiteDialog() {
  ui::DialogModel::Builder builder;
  constrained_window::ShowBrowserModal(
      builder.SetTitle(u"Add website to Flux")
          .AddTextfield(kAddSiteNameFieldId, u"Name", std::u16string(),
                        ui::DialogModelTextfield::Params().SetAccessibleName(
                            u"Website name"))
          .AddTextfield(kAddSiteUrlFieldId, u"URL", std::u16string(),
                        ui::DialogModelTextfield::Params().SetAccessibleName(
                            u"Website URL"))
          .AddOkButton(
              base::BindOnce(
                  [](base::WeakPtr<FluxSidebarView> view,
                     ui::DialogModel* model) {
                    if (!view) {
                      return;
                    }
                    GURL parsed(base::UTF16ToUTF8(
                        model->GetTextfieldByUniqueId(kAddSiteUrlFieldId)
                            ->text()));
                    if (!parsed.has_scheme()) {
                      parsed = GURL(
                          "https://" +
                          base::UTF16ToUTF8(
                              model->GetTextfieldByUniqueId(kAddSiteUrlFieldId)
                                  ->text()));
                    }
                    view->AddSite(
                        std::u16string(
                            model->GetTextfieldByUniqueId(kAddSiteNameFieldId)
                                ->text()),
                        std::move(parsed));
                  },
                  weak_ptr_factory_.GetWeakPtr(), builder.model()),
              ui::DialogModel::Button::Params().SetLabel(u"Add website"))
          .AddCancelButton(base::DoNothing())
          .SetInitiallyFocusedField(kAddSiteNameFieldId)
          .Build(),
      browser_view_->GetNativeWindow());
}

void FluxSidebarView::AddSite(std::u16string name, GURL url) {
  if (!model_.AddSite(std::move(name), url)) {
    return;
  }
  ActivateSite(model_.sites().back().id);
}

void FluxSidebarView::UpdateVisiblePanels() {
  for (const std::string& id : active_site_ids_) {
    if (const FluxSite* site = model_.FindSite(id)) {
      GetOrCreateSession(*site)->ApplySiteState(*site);
    }
  }

  for (FluxSitePanel* session : sessions_) {
    const bool active = IsActive(session->site_id());
    session->SetVisible(active);
    if (active) {
      session->StopDiscardTimer();
    } else {
      session->StartDiscardTimer(base::BindOnce(&FluxSidebarView::DiscardSite,
                                                weak_ptr_factory_.GetWeakPtr(),
                                                session->site_id()));
    }
  }

  panel_host_->SetVisible(IsPanelOpen());
  resize_area_->SetVisible(IsPanelOpen());
  UpdatePanelLayout();
  InvalidateBrowserLayout();
}

void FluxSidebarView::UpdatePanelLayout() {
  panel_layout_->SetOrientation(
      split_ && split_vertical_ ? views::BoxLayout::Orientation::kVertical
                                : views::BoxLayout::Orientation::kHorizontal);
  for (FluxSitePanel* session : sessions_) {
    panel_layout_->SetFlexForView(session, session->GetVisible() ? 1 : 0);
  }
  panel_host_->InvalidateLayout();
}

void FluxSidebarView::DiscardSite(const std::string& id) {
  FluxSitePanel* session = FindSession(id);
  if (!session || IsActive(id)) {
    return;
  }
  std::erase(sessions_, session);
  panel_host_->RemoveChildViewT(session);
}

FluxSitePanel* FluxSidebarView::FindSession(const std::string& id) const {
  auto it = std::ranges::find(sessions_, id, &FluxSitePanel::site_id);
  return it == sessions_.end() ? nullptr : *it;
}

FluxSitePanel* FluxSidebarView::GetOrCreateSession(const FluxSite& site) {
  if (FluxSitePanel* existing = FindSession(site.id)) {
    return existing;
  }
  auto panel = std::make_unique<FluxSitePanel>(
      browser_view_, site,
      base::BindRepeating(&FluxSidebarModel::UpdateLastUrl,
                          base::Unretained(&model_), site.id));
  FluxSitePanel* panel_ptr = panel_host_->AddChildView(std::move(panel));
  sessions_.push_back(panel_ptr);
  return panel_ptr;
}

bool FluxSidebarView::IsActive(const std::string& id) const {
  return std::ranges::find(active_site_ids_, id) != active_site_ids_.end();
}

void FluxSidebarView::InvalidateBrowserLayout() {
  InvalidateLayout();
  if (parent()) {
    parent()->InvalidateLayout();
  }
}

void FluxSidebarView::Layout(PassKey) {
  // The sidebar is docked on the left. Hang the handle off the popout's right
  // edge into the browser; do not steal width from the popout itself.
  const int handle_width = GetResizeHandleWidth();
  const int panel_width = std::max(0, width() - kRailWidth - handle_width);
  rail_->SetBounds(0, 0, kRailWidth, height());
  panel_host_->SetBounds(kRailWidth, 0, panel_width, height());
  resize_area_->SetBounds(kRailWidth + panel_width, 0, handle_width, height());
  rail_->DeprecatedLayoutImmediately();
  panel_host_->DeprecatedLayoutImmediately();
}

void FluxSidebarView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateLogoColor();
}

void FluxSidebarView::UpdateLogoColor() {
  if (!logo_button_) {
    return;
  }
  const ui::ColorProvider* const color_provider = GetColorProvider();
  const SkColor f_color =
      color_provider && color_utils::IsDark(
                            color_provider->GetColor(ui::kColorSysSurface2))
          ? SK_ColorWHITE
          : SK_ColorBLACK;
  const ui::ImageModel image = ui::ImageModel::FromVectorIcon(
      kFluxSidebarLogoIcon, f_color, kLogoIconSize);
  logo_button_->SetImageModel(views::Button::STATE_NORMAL, image);
  logo_button_->SetImageModel(views::Button::STATE_HOVERED, image);
  logo_button_->SetImageModel(views::Button::STATE_PRESSED, image);
  logo_button_->SetImageModel(views::Button::STATE_DISABLED, image);
}

void FluxSidebarView::OnEnabledChanged() {
  const bool enabled = browser_view_->GetProfile()->GetPrefs()->GetBoolean(
      prefs::kFluxSidebarEnabled);
  SetVisible(enabled);
  if (!enabled) {
    active_site_ids_.clear();
    split_ = false;
    UpdateVisiblePanels();
  } else {
    LoadFavicons();
  }
  InvalidateBrowserLayout();
}

void FluxSidebarView::LoadFavicons() {
  favicon::FaviconService* const service = FaviconServiceFactory::GetForProfile(
      browser_view_->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
  if (!service) {
    return;
  }
  for (const FluxSite& site : model_.sites()) {
    // Only ask for what we are missing; OnFaviconLoaded() rebuilds the rail,
    // so re-requesting cached sites would spin.
    if (favicons_.contains(site.id)) {
      continue;
    }
    service->GetFaviconImageForPageURL(
        site.url,
        base::BindOnce(&FluxSidebarView::OnFaviconLoaded,
                       weak_ptr_factory_.GetWeakPtr(), site.id),
        &favicon_task_tracker_);
  }
}

void FluxSidebarView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  const auto it = button_site_ids_.find(source);
  if (it == button_site_ids_.end()) {
    return;
  }
  const FluxSite* const site = model_.FindSite(it->second);
  if (!site) {
    return;
  }
  context_menu_site_id_ = it->second;

  context_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  context_menu_model_->AddItem(kChooseIconFile, u"Choose icon…");
  context_menu_model_->AddItem(kUseIconUrl, u"Use image URL…");
  if (!site->icon.empty()) {
    context_menu_model_->AddItem(kResetIcon, u"Reset to site favicon");
  }
  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(
      GetWidget(), nullptr, gfx::Rect(point, gfx::Size()),
      views::MenuAnchorPosition::kTopLeft, source_type);
}

void FluxSidebarView::ExecuteCommand(int command_id, int event_flags) {
  if (context_menu_site_id_.empty()) {
    return;
  }
  pending_icon_site_id_ = context_menu_site_id_;
  switch (command_id) {
    case kChooseIconFile:
      ChooseIconFile();
      break;
    case kUseIconUrl:
      ShowIconUrlDialog();
      break;
    case kResetIcon:
      model_.SetIcon(pending_icon_site_id_, std::string());
      pending_icon_site_id_.clear();
      break;
    default:
      pending_icon_site_id_.clear();
      break;
  }
}

void FluxSidebarView::ChooseIconFile() {
  if (select_file_dialog_) {
    return;
  }
  select_file_dialog_ = ui::SelectFileDialog::Create(this, nullptr);
  ui::SelectFileDialog::FileTypeInfo file_types;
  file_types.extensions = {{FILE_PATH_LITERAL("png"), FILE_PATH_LITERAL("jpg"),
                            FILE_PATH_LITERAL("jpeg"),
                            FILE_PATH_LITERAL("webp"), FILE_PATH_LITERAL("gif"),
                            FILE_PATH_LITERAL("ico")}};
  select_file_dialog_->SelectFile(
      ui::SelectFileDialog::SELECT_OPEN_FILE, u"Choose site icon",
      base::FilePath(), &file_types, /*file_type_index=*/0,
      base::FilePath::StringType(), browser_view_->GetNativeWindow());
}

void FluxSidebarView::FileSelected(const ui::SelectedFileInfo& file,
                                   int index) {
  select_file_dialog_.reset();
  // Reading the file has to happen off the UI thread.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ReadFileBytes, file.path()),
      base::BindOnce(&FluxSidebarView::OnIconBytesRead,
                     weak_ptr_factory_.GetWeakPtr()));
}

void FluxSidebarView::FileSelectionCanceled() {
  select_file_dialog_.reset();
  pending_icon_site_id_.clear();
}

void FluxSidebarView::ShowIconUrlDialog() {
  ui::DialogModel::Builder builder;
  ui::DialogModel* const model = builder.model();
  constrained_window::ShowBrowserModal(
      builder.SetTitle(u"Use image URL")
          .AddTextfield(kIconUrlFieldId, u"Image URL", std::u16string(),
                        ui::DialogModelTextfield::Params().SetAccessibleName(
                            u"Image URL"))
          .AddOkButton(base::BindOnce(
              [](base::WeakPtr<FluxSidebarView> self, ui::DialogModel* model) {
                if (self) {
                  self->FetchIconFromUrl(GURL(
                      model->GetTextfieldByUniqueId(kIconUrlFieldId)->text()));
                }
              },
              weak_ptr_factory_.GetWeakPtr(), base::Unretained(model)))
          .Build(),
      browser_view_->GetNativeWindow());
}

void FluxSidebarView::FetchIconFromUrl(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    pending_icon_site_id_.clear();
    return;
  }
  constexpr net::NetworkTrafficAnnotationTag kAnnotation =
      net::DefineNetworkTrafficAnnotation("flux_sidebar_custom_icon", R"(
        semantics {
          sender: "Flux Sidebar"
          description:
            "Downloads an image the user explicitly chose as the icon for a "
            "site pinned to the Flux sidebar."
          trigger:
            "The user picks 'Use image URL' on a Flux sidebar site and enters "
            "an image address."
          data: "None. This is an unauthenticated fetch of the given URL."
          destination: OTHER
          internal { contacts { email: "zion@ziona.dev" } }
          user_data { type: NONE }
          last_reviewed: "2026-09-01"
        }
        policy {
          cookies_allowed: NO
          setting: "Only performed when the user supplies an image URL."
          policy_exception_justification: "Not implemented, user initiated."
        })");

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  icon_loader_ =
      network::SimpleURLLoader::Create(std::move(request), kAnnotation);
  icon_loader_->DownloadToString(
      browser_view_->GetProfile()
          ->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess()
          .get(),
      base::BindOnce(&FluxSidebarView::OnIconBytesRead,
                     weak_ptr_factory_.GetWeakPtr()),
      kMaxIconSourceBytes);
}

void FluxSidebarView::OnIconBytesRead(std::optional<std::string> bytes) {
  // Null for the file path; holds the finished loader for the URL path.
  icon_loader_.reset();
  if (!bytes || bytes->empty()) {
    pending_icon_site_id_.clear();
    return;
  }
  DecodeIconBytes(*bytes);
}

void FluxSidebarView::DecodeIconBytes(const std::string& bytes) {
  // The bytes are user-supplied, so decode them out of process.
  data_decoder::DecodeImageIsolated(
      base::as_byte_span(bytes), data_decoder::mojom::ImageCodec::kDefault,
      /*shrink_to_fit=*/true, kMaxIconSourceBytes,
      gfx::Size(kCustomIconSizePx, kCustomIconSizePx),
      base::BindOnce(&FluxSidebarView::OnIconDecoded,
                     weak_ptr_factory_.GetWeakPtr()));
}

void FluxSidebarView::OnIconDecoded(const SkBitmap& bitmap) {
  const std::string site_id = pending_icon_site_id_;
  pending_icon_site_id_.clear();
  if (bitmap.isNull() || site_id.empty() || !model_.FindSite(site_id)) {
    return;
  }
  const SkBitmap square =
      skia::ImageOperations::Resize(bitmap, skia::ImageOperations::RESIZE_BEST,
                                    kCustomIconSizePx, kCustomIconSizePx);
  // Re-encode rather than keeping the original bytes: it normalises format and
  // size, and means GetCustomIcon() only ever decodes PNGs we produced.
  const std::optional<std::vector<uint8_t>> png =
      gfx::PNGCodec::EncodeBGRASkBitmap(square, /*discard_transparency=*/false);
  if (!png) {
    return;
  }
  model_.SetIcon(site_id,
                 base::StrCat({kIconDataUrlPrefix, base::Base64Encode(*png)}));
}

gfx::ImageSkia FluxSidebarView::GetCustomIcon(const FluxSite& site) {
  if (site.icon.empty()) {
    return gfx::ImageSkia();
  }
  const auto cached = decoded_icons_.find(site.icon);
  if (cached != decoded_icons_.end()) {
    return cached->second;
  }
  std::string_view encoded(site.icon);
  if (!encoded.starts_with(kIconDataUrlPrefix)) {
    return gfx::ImageSkia();
  }
  encoded.remove_prefix(std::string_view(kIconDataUrlPrefix).size());
  const std::optional<std::vector<uint8_t>> bytes = base::Base64Decode(encoded);
  if (!bytes) {
    return gfx::ImageSkia();
  }
  // Safe to decode in process: OnIconDecoded() produced this PNG, and the
  // untrusted original already went through the data decoder service.
  const SkBitmap bitmap = gfx::PNGCodec::Decode(*bytes);
  if (bitmap.isNull()) {
    return gfx::ImageSkia();
  }
  gfx::ImageSkia image = gfx::ImageSkia::CreateFrom1xBitmap(bitmap);
  decoded_icons_[site.icon] = image;
  return image;
}

void FluxSidebarView::OnFaviconLoaded(
    const std::string& id,
    const favicon_base::FaviconImageResult& result) {
  const FluxSite* const site = model_.FindSite(id);
  if (!site) {
    return;
  }
  if (result.image.IsEmpty()) {
    FetchFavicon(id, site->url);
    return;
  }
  favicons_[id] = result.image;
  RebuildRail();
}

void FluxSidebarView::FetchFavicon(const std::string& id,
                                   const GURL& page_url) {
  if (!page_url.SchemeIsHTTPOrHTTPS() ||
      !favicon_download_attempts_.insert(id).second) {
    return;
  }

  DownloadFaviconImage(id, page_url.GetWithEmptyPath().Resolve("favicon.ico"));
}

void FluxSidebarView::DownloadFaviconImage(const std::string& id,
                                           const GURL& icon_url) {
  constexpr net::NetworkTrafficAnnotationTag kAnnotation =
      net::DefineNetworkTrafficAnnotation("flux_sidebar_favicon", R"(
        semantics {
          sender: "Flux Sidebar"
          description:
            "Downloads a favicon for a site pinned to the Flux sidebar when "
            "Chrome has no cached favicon for that site."
          trigger:
            "A browser window displays an enabled Flux sidebar containing a "
            "site whose favicon is not already in the profile database."
          data: "The origin of a site the user pinned to the sidebar."
          destination: WEBSITE
          internal { contacts { email: "zion@ziona.dev" } }
          user_data { type: WEB_CONTENT }
          last_reviewed: "2026-09-01"
        }
        policy {
          cookies_allowed: NO
          setting:
            "Users can disable the Flux sidebar in the Flux Features section "
            "of Chrome settings."
          policy_exception_justification: "Not implemented."
        })");

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = icon_url;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->headers.SetHeader(net::HttpRequestHeaders::kUserAgent,
                             embedder_support::GetUserAgent());
  auto loader =
      network::SimpleURLLoader::Create(std::move(request), kAnnotation);
  network::SimpleURLLoader* const loader_ptr = loader.get();
  favicon_loaders_[id] = std::move(loader);
  loader_ptr->DownloadToString(
      browser_view_->GetProfile()
          ->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess()
          .get(),
      base::BindOnce(&FluxSidebarView::OnFaviconBytesRead,
                     weak_ptr_factory_.GetWeakPtr(), id),
      kMaxFaviconBytes);
}

void FluxSidebarView::OnFaviconBytesRead(const std::string& id,
                                         std::optional<std::string> bytes) {
  favicon_loaders_.erase(id);
  if (!bytes || bytes->empty() || !model_.FindSite(id)) {
    FetchFaviconFromPage(id);
    return;
  }
  data_decoder::DecodeImageIsolated(
      base::as_byte_span(*bytes), data_decoder::mojom::ImageCodec::kDefault,
      /*shrink_to_fit=*/true, kMaxFaviconBytes,
      gfx::Size(kCustomIconSizePx, kCustomIconSizePx),
      base::BindOnce(&FluxSidebarView::OnDownloadedFaviconDecoded,
                     weak_ptr_factory_.GetWeakPtr(), id));
}

void FluxSidebarView::OnDownloadedFaviconDecoded(const std::string& id,
                                                 const SkBitmap& bitmap) {
  if (bitmap.isNull() || !model_.FindSite(id)) {
    FetchFaviconFromPage(id);
    return;
  }
  favicons_[id] = gfx::Image::CreateFrom1xBitmap(bitmap);
  RebuildRail();
}

void FluxSidebarView::FetchFaviconFromPage(const std::string& id) {
  const FluxSite* const site = model_.FindSite(id);
  if (!site || !site->url.SchemeIsHTTPOrHTTPS() ||
      !favicon_page_attempts_.insert(id).second) {
    return;
  }

  constexpr net::NetworkTrafficAnnotationTag kAnnotation =
      net::DefineNetworkTrafficAnnotation("flux_sidebar_favicon_page", R"(
        semantics {
          sender: "Flux Sidebar"
          description:
            "Reads a pinned site's page to find its declared favicon when the "
            "conventional favicon URL does not return a usable image."
          trigger:
            "A browser window displays an enabled Flux sidebar containing a "
            "site whose favicon is not already in the profile database."
          data: "The URL of a site the user pinned to the sidebar."
          destination: WEBSITE
          internal { contacts { email: "zion@ziona.dev" } }
          user_data { type: WEB_CONTENT }
          last_reviewed: "2026-09-01"
        }
        policy {
          cookies_allowed: NO
          setting:
            "Users can disable the Flux sidebar in the Flux Features section "
            "of Chrome settings."
          policy_exception_justification: "Not implemented."
        })");

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = site->url;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->headers.SetHeader(net::HttpRequestHeaders::kUserAgent,
                             embedder_support::GetUserAgent());
  auto loader =
      network::SimpleURLLoader::Create(std::move(request), kAnnotation);
  network::SimpleURLLoader* const loader_ptr = loader.get();
  favicon_loaders_[id] = std::move(loader);
  loader_ptr->DownloadToString(
      browser_view_->GetProfile()
          ->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess()
          .get(),
      base::BindOnce(&FluxSidebarView::OnFaviconPageRead,
                     weak_ptr_factory_.GetWeakPtr(), id),
      kMaxFaviconPageBytes);
}

void FluxSidebarView::OnFaviconPageRead(
    const std::string& id,
    std::optional<std::string> html) {
  favicon_loaders_.erase(id);
  const FluxSite* const site = model_.FindSite(id);
  if (!site || !html || html->empty()) {
    return;
  }
  const std::optional<GURL> icon_url = FindDeclaredFavicon(*html, site->url);
  if (icon_url) {
    DownloadFaviconImage(id, *icon_url);
  }
}

BEGIN_METADATA(FluxSidebarView)
END_METADATA

}  // namespace flux_sidebar
