// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/flux_sidebar/flux_sidebar_model.h"

#include "base/functional/bind.h"
#include "base/values.h"
#include "chrome/common/flux_sidebar_pref_names.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace flux_sidebar {
namespace {

class FluxSidebarModelTest : public testing::Test {
 protected:
  void SetUp() override {
    prefs_.registry()->RegisterListPref(prefs::kFluxSidebarSites);
  }

  TestingPrefServiceSimple prefs_;
};

TEST_F(FluxSidebarModelTest, StartsEmpty) {
  FluxSidebarModel model(&prefs_);
  EXPECT_TRUE(model.sites().empty());
}

TEST_F(FluxSidebarModelTest, RemovesLegacyDefaultSites) {
  base::ListValue list;
  for (const char* id : {"chatgpt", "gemini", "gmail", "discord", "linear"}) {
    base::DictValue dict;
    dict.Set("id", id);
    dict.Set("name", id);
    dict.Set("url", "https://example.com/");
    dict.Set("mark", "X");
    dict.Set("color", "#a8ff78");
    list.Append(std::move(dict));
  }
  prefs_.SetList(prefs::kFluxSidebarSites, std::move(list));

  FluxSidebarModel model(&prefs_);
  ASSERT_EQ(1u, model.sites().size());
  EXPECT_EQ("linear", model.sites().front().id);
}

TEST_F(FluxSidebarModelTest, DoesNotOverwriteExistingSyncedSites) {
  base::ListValue list;
  base::DictValue dict;
  dict.Set("id", "linear");
  dict.Set("name", "Linear");
  dict.Set("url", "https://linear.app/");
  dict.Set("mark", "L");
  dict.Set("color", "#a8ff78");
  list.Append(std::move(dict));
  prefs_.SetList(prefs::kFluxSidebarSites, std::move(list));

  FluxSidebarModel model(&prefs_);
  ASSERT_EQ(1u, model.sites().size());
  EXPECT_EQ("linear", model.sites().front().id);
}

TEST_F(FluxSidebarModelTest, AddRejectsPrivilegedSchemes) {
  FluxSidebarModel model(&prefs_);
  EXPECT_TRUE(model.sites().empty());
  EXPECT_FALSE(model.AddSite(u"Settings", GURL("chrome://settings")));
  EXPECT_FALSE(model.AddSite(u"Empty", GURL()));
  EXPECT_TRUE(model.AddSite(u"Example", GURL("https://example.com")));
  ASSERT_EQ(1u, model.sites().size());
  EXPECT_EQ(u"E", model.sites().front().mark);
  EXPECT_FALSE(model.sites().front().muted);
}

TEST_F(FluxSidebarModelTest, RemoveMoveMuteAndLastUrlPersist) {
  FluxSidebarModel model(&prefs_);
  ASSERT_TRUE(model.AddSite(u"One", GURL("https://one.example/")));
  ASSERT_TRUE(model.AddSite(u"Two", GURL("https://two.example/")));
  const std::string first_id = model.sites().front().id;
  const std::string second_id = model.sites().back().id;

  model.UpdateLastUrl(first_id, GURL("https://one.example/inbox"));
  model.SetMuted(second_id, true);
  ASSERT_TRUE(model.MoveSite(second_id, 0));
  EXPECT_EQ(second_id, model.sites().front().id);
  EXPECT_TRUE(model.sites().front().muted);
  EXPECT_EQ("https://one.example/inbox", model.sites().back().url.spec());

  FluxSidebarModel reloaded(&prefs_);
  ASSERT_EQ(2u, reloaded.sites().size());
  EXPECT_EQ(second_id, reloaded.sites().front().id);
  EXPECT_TRUE(reloaded.sites().front().muted);
  EXPECT_EQ("https://one.example/inbox", reloaded.sites().back().url.spec());

  EXPECT_TRUE(reloaded.RemoveSite(first_id));
  EXPECT_EQ(1u, reloaded.sites().size());
}

TEST_F(FluxSidebarModelTest, LastUrlDoesNotNotifyUi) {
  FluxSidebarModel model(&prefs_);
  ASSERT_TRUE(model.AddSite(u"One", GURL("https://one.example/")));
  int notifies = 0;
  auto subscription = model.AddChangedCallback(base::BindRepeating(
      [](int* notifies) { ++*notifies; }, &notifies));
  model.UpdateLastUrl(model.sites().front().id,
                      GURL("https://one.example/inbox"));
  EXPECT_EQ(0, notifies);
  EXPECT_EQ("https://one.example/inbox", model.sites().front().url.spec());
}

}  // namespace
}  // namespace flux_sidebar
