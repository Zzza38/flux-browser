// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/html/media/flux_picture_in_picture_overlay.h"

#include "third_party/blink/public/strings/grit/blink_strings.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/events/event.h"
#include "third_party/blink/renderer/core/dom/events/native_event_listener.h"
#include "third_party/blink/renderer/core/event_type_names.h"
#include "third_party/blink/renderer/core/frame/picture_in_picture_controller.h"
#include "third_party/blink/renderer/core/html/media/html_video_element.h"
#include "third_party/blink/renderer/core/html_names.h"
#include "third_party/blink/renderer/core/keywords.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/text/platform_locale.h"
#include "ui/strings/grit/ax_strings.h"

namespace blink {

// Runs the button and, just as importantly, keeps the page from seeing the
// clicks that land on it. The button sits on top of whatever click handling
// the site has wired to its own player, so a click here must not also pause
// the video or open a lightbox.
class FluxPictureInPictureOverlay::ButtonEventListener final
    : public NativeEventListener {
 public:
  explicit ButtonEventListener(FluxPictureInPictureOverlay* overlay)
      : overlay_(overlay) {}

  void Invoke(ExecutionContext*, Event* event) override {
    overlay_->HandleButtonEvent(*event);
  }

  void Trace(Visitor* visitor) const override {
    visitor->Trace(overlay_);
    NativeEventListener::Trace(visitor);
  }

 private:
  Member<FluxPictureInPictureOverlay> overlay_;
};

class FluxPictureInPictureOverlay::VideoHoverEventListener final
    : public NativeEventListener {
 public:
  explicit VideoHoverEventListener(FluxPictureInPictureOverlay* overlay)
      : overlay_(overlay) {}

  void Invoke(ExecutionContext*, Event* event) override {
    overlay_->HandleVideoHoverEvent(*event);
  }

  void Trace(Visitor* visitor) const override {
    visitor->Trace(overlay_);
    NativeEventListener::Trace(visitor);
  }

 private:
  Member<FluxPictureInPictureOverlay> overlay_;
};

FluxPictureInPictureOverlay::FluxPictureInPictureOverlay(
    HTMLVideoElement& video_element)
    : HTMLDivElement(video_element.GetDocument()),
      video_element_(&video_element) {
  SetShadowPseudoId(AtomicString("-internal-flux-picture-in-picture-overlay"));

  button_ = MakeGarbageCollected<HTMLDivElement>(GetDocument());
  button_->SetShadowPseudoId(
      AtomicString("-internal-flux-picture-in-picture-button"));
  button_->setAttribute(html_names::kRoleAttr, keywords::kButton);
  ParserAppendChild(button_);

  button_event_listener_ = MakeGarbageCollected<ButtonEventListener>(this);
  button_->addEventListener(event_type_names::kClick, button_event_listener_);

  video_hover_event_listener_ =
      MakeGarbageCollected<VideoHoverEventListener>(this);
  video_element_->addEventListener(event_type_names::kMouseenter,
                                   video_hover_event_listener_);
  video_element_->addEventListener(event_type_names::kMouseleave,
                                   video_hover_event_listener_);

  UpdateButtonLabel();
}

void FluxPictureInPictureOverlay::Detach() {
  video_element_->removeEventListener(event_type_names::kMouseenter,
                                      video_hover_event_listener_.Get(), false);
  video_element_->removeEventListener(event_type_names::kMouseleave,
                                      video_hover_event_listener_.Get(), false);
}

void FluxPictureInPictureOverlay::UpdateButtonLabel() {
  const bool in_picture_in_picture =
      PictureInPictureController::IsElementInPictureInPicture(
          video_element_.Get());
  const String label = video_element_->GetLocale().QueryString(
      in_picture_in_picture ? IDS_AX_MEDIA_EXIT_PICTURE_IN_PICTURE_BUTTON
                            : IDS_AX_MEDIA_ENTER_PICTURE_IN_PICTURE_BUTTON);

  button_->setAttribute(html_names::kAriaLabelAttr, AtomicString(label));
  button_->setAttribute(html_names::kTitleAttr, AtomicString(label));
}

void FluxPictureInPictureOverlay::HandleVideoHoverEvent(Event& event) {
  const bool hovered = event.type() == event_type_names::kMouseenter;
  if (hovered_ == hovered) {
    return;
  }
  hovered_ = hovered;
  UpdateClassAttribute();
}

void FluxPictureInPictureOverlay::UpdateClassAttribute() {
  button_->setAttribute(html_names::kClassAttr,
                        hovered_ ? AtomicString("visible") : g_empty_atom);
}

void FluxPictureInPictureOverlay::HandleButtonEvent(Event& event) {
  // Keep the page's video click handler from also pausing playback or opening
  // its own UI.
  event.stopPropagation();
  event.preventDefault();

  PictureInPictureController& controller =
      PictureInPictureController::From(video_element_->GetDocument());
  if (PictureInPictureController::IsElementInPictureInPicture(
          video_element_.Get())) {
    controller.ExitPictureInPicture(video_element_.Get(), /*resolver=*/nullptr);
  } else {
    controller.EnterPictureInPicture(video_element_.Get(), /*promise=*/nullptr);
  }

  UpdateButtonLabel();
}

void FluxPictureInPictureOverlay::Trace(Visitor* visitor) const {
  visitor->Trace(button_);
  visitor->Trace(button_event_listener_);
  visitor->Trace(video_hover_event_listener_);
  visitor->Trace(video_element_);
  HTMLDivElement::Trace(visitor);
}

}  // namespace blink
