// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/flux_rgb_mode/flux_rgb_mode_controller.h"

#include "base/time/time.h"
#include "cc/paint/filter_operation.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace flux_rgb_mode {
namespace {

TEST(FluxRgbModeControllerTest, AdvanceHueScalesWithElapsedTime) {
  EXPECT_FLOAT_EQ(9.0f, AdvanceHue(0.0f, 90, base::Milliseconds(100)));
  EXPECT_FLOAT_EQ(4.5f, AdvanceHue(0.0f, 90, base::Milliseconds(50)));
}

TEST(FluxRgbModeControllerTest, AdvanceHueWrapsAtFullCircle) {
  EXPECT_FLOAT_EQ(62.0f, AdvanceHue(350.0f, 720, base::Milliseconds(100)));
}

TEST(FluxRgbModeControllerTest, AdvanceHueClampsSpeed) {
  // A pref value far above the slider maximum still sweeps at 720 deg/s.
  EXPECT_FLOAT_EQ(72.0f, AdvanceHue(0.0f, 100000, base::Milliseconds(100)));

  // A pref value below the slider minimum still sweeps at 15 deg/s.
  EXPECT_FLOAT_EQ(1.5f, AdvanceHue(0.0f, 0, base::Milliseconds(100)));
}

TEST(FluxRgbModeControllerTest, AdvanceHueClampsStep) {
  // A frame that arrives very late is capped, so the hue never jumps.
  EXPECT_FLOAT_EQ(AdvanceHue(0.0f, 720, base::Milliseconds(100)),
                  AdvanceHue(0.0f, 720, base::Seconds(30)));

  // A backwards delta cannot rewind the sweep.
  EXPECT_FLOAT_EQ(0.0f, AdvanceHue(0.0f, 90, base::Seconds(-1)));
}

TEST(FluxRgbModeControllerTest, ColorMatrixLeavesAlphaAlone) {
  const cc::FilterOperation::Matrix matrix = BuildRgbColorMatrix(0.0f);
  EXPECT_FLOAT_EQ(0.0f, matrix[15]);
  EXPECT_FLOAT_EQ(0.0f, matrix[16]);
  EXPECT_FLOAT_EQ(0.0f, matrix[17]);
  EXPECT_FLOAT_EQ(1.0f, matrix[18]);
  EXPECT_FLOAT_EQ(0.0f, matrix[19]);
}

TEST(FluxRgbModeControllerTest, ColorMatrixTintsTowardTheHue) {
  // Hue 0 is red, so the red output row picks up the tint and the others do
  // not.
  const cc::FilterOperation::Matrix red = BuildRgbColorMatrix(0.0f);
  EXPECT_GT(red[0], red[6]);
  EXPECT_GT(red[0], red[12]);

  // A third of the way around the wheel is green, which moves the tint onto
  // the green output row.
  const cc::FilterOperation::Matrix green = BuildRgbColorMatrix(120.0f);
  EXPECT_GT(green[6], green[0]);
  EXPECT_GT(green[6], green[12]);
}

TEST(FluxRgbModeControllerTest, ColorMatrixIgnoresHueWinding) {
  EXPECT_EQ(BuildRgbColorMatrix(30.0f), BuildRgbColorMatrix(390.0f));
  EXPECT_EQ(BuildRgbColorMatrix(30.0f), BuildRgbColorMatrix(-330.0f));
}

}  // namespace
}  // namespace flux_rgb_mode
