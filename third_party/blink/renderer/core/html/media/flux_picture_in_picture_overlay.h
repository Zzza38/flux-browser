// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_HTML_MEDIA_FLUX_PICTURE_IN_PICTURE_OVERLAY_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_HTML_MEDIA_FLUX_PICTURE_IN_PICTURE_OVERLAY_H_

#include "third_party/blink/renderer/core/html/html_div_element.h"

namespace blink {

class Event;
class HTMLVideoElement;

// Flux: a picture-in-picture button drawn over the top right corner of a
// <video>, whether or not the page asked for controls. The button stays hidden
// until the video is hovered; the showing and hiding is done by
// fluxPipOverlay.css. DOM structure looks like:
//
// FluxPictureInPictureOverlay
//     (-internal-flux-picture-in-picture-overlay)
// \-HTMLDivElement
//      (-internal-flux-picture-in-picture-button)
class FluxPictureInPictureOverlay final : public HTMLDivElement {
 public:
  explicit FluxPictureInPictureOverlay(HTMLVideoElement&);

  HTMLVideoElement& GetVideoElement() const { return *video_element_; }

  // Points the button's label and tooltip at whichever direction the click
  // will take the video.
  void UpdateButtonLabel();

  // Stops observing the video before the overlay is removed at runtime.
  void Detach();

  // Element:
  void Trace(Visitor*) const override;

 private:
  class ButtonEventListener;
  class VideoHoverEventListener;

  // Node override.
  bool IsFluxPictureInPictureOverlay() const override { return true; }

  void HandleVideoHoverEvent(Event&);
  void UpdateClassAttribute();

  void HandleButtonEvent(Event&);

  Member<HTMLDivElement> button_;
  Member<ButtonEventListener> button_event_listener_;
  Member<VideoHoverEventListener> video_hover_event_listener_;
  Member<HTMLVideoElement> video_element_;
  bool hovered_ = false;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_HTML_MEDIA_FLUX_PICTURE_IN_PICTURE_OVERLAY_H_
