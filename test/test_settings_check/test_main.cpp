// Host unit tests for the checks on a project's settings: pio test -e native
#include <unity.h>

#include "SettingsCheck.h"

void setUp() {}
void tearDown() {}

static void test_keys() {
  TEST_ASSERT_TRUE(SettingsCheck::validKey("map_key"));
  TEST_ASSERT_TRUE(SettingsCheck::validKey("a23456789012345"));    // 15
  TEST_ASSERT_FALSE(SettingsCheck::validKey("a234567890123456"));  // 16
  TEST_ASSERT_FALSE(SettingsCheck::validKey(""));
  TEST_ASSERT_FALSE(SettingsCheck::validKey("Map"));
  TEST_ASSERT_FALSE(SettingsCheck::validKey("map key"));
  TEST_ASSERT_FALSE(SettingsCheck::validKey(nullptr));
}

static void test_choices() {
  TEST_ASSERT_TRUE(SettingsCheck::inChoices("sta,ap,apsta", "ap"));
  TEST_ASSERT_FALSE(SettingsCheck::inChoices("sta,ap,apsta", "aps"));
  TEST_ASSERT_FALSE(SettingsCheck::inChoices("sta,ap,apsta", ""));
  TEST_ASSERT_FALSE(SettingsCheck::inChoices("", "x"));
}

static void test_values() {
  TEST_ASSERT_NULL(SettingsCheck::valueError(SettingKind::Text, "", 8, "12345678"));
  TEST_ASSERT_EQUAL_STRING("too long", SettingsCheck::valueError(SettingKind::Text, "", 8, "123456789"));
  TEST_ASSERT_NULL(SettingsCheck::valueError(SettingKind::Text, "", 63, "a \"quoted\" <b>&amp; \xC3\xA9"));
  TEST_ASSERT_EQUAL_STRING("not valid UTF-8", SettingsCheck::valueError(SettingKind::Text, "", 63, "\xC3"));
  TEST_ASSERT_EQUAL_STRING("not one of the choices", SettingsCheck::valueError(SettingKind::Choice, "a,b", 63, "c"));
  TEST_ASSERT_NULL(SettingsCheck::valueError(SettingKind::Secret, "", 63, "s3cret"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_keys);
  RUN_TEST(test_choices);
  RUN_TEST(test_values);
  return UNITY_END();
}
