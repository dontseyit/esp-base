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

static const char* num(const char* v, double min, double max, uint8_t decimals) {
  return SettingsCheck::valueError(SettingKind::Number, "", 63, v, min, max, decimals);
}

static void test_numbers() {
  TEST_ASSERT_NULL(num("0", 0, 100, 0));
  TEST_ASSERT_NULL(num("100", 0, 100, 0));
  TEST_ASSERT_NULL(num("007", 0, 100, 0));
  TEST_ASSERT_EQUAL_STRING("out of range", num("101", 0, 100, 0));
  TEST_ASSERT_EQUAL_STRING("out of range", num("-1", 0, 100, 0));
  TEST_ASSERT_EQUAL_STRING("not a whole number", num("50.5", 0, 100, 0));
  // Only the plain form, so what is stored reads back the same everywhere.
  const char* bad[] = {"", "-", "abc", "1e3", "+5", " 5", "5 ", "5.", ".5", "0x10", "1,5", "--1"};
  for (const char* v : bad) {
    TEST_ASSERT_EQUAL_STRING_MESSAGE("not a number", num(v, -1000, 1000, 2), v);
  }
  TEST_ASSERT_NULL(num("-40", -40, 85, 1));
  TEST_ASSERT_NULL(num("21.5", -40, 85, 1));
  TEST_ASSERT_NULL(num("85.0", -40, 85, 1));
  TEST_ASSERT_NULL(num("-0.5", -40, 85, 1));
  TEST_ASSERT_EQUAL_STRING("too many decimals", num("21.55", -40, 85, 1));
  TEST_ASSERT_EQUAL_STRING("out of range", num("85.1", -40, 85, 1));
}

static void test_bools() {
  TEST_ASSERT_NULL(SettingsCheck::valueError(SettingKind::Bool, "", 63, "true"));
  TEST_ASSERT_NULL(SettingsCheck::valueError(SettingKind::Bool, "", 63, "false"));
  const char* bad[] = {"", "on", "1", "TRUE", "yes", "true "};
  for (const char* v : bad) {
    TEST_ASSERT_EQUAL_STRING_MESSAGE("not true or false", SettingsCheck::valueError(SettingKind::Bool, "", 63, v), v);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_keys);
  RUN_TEST(test_choices);
  RUN_TEST(test_values);
  RUN_TEST(test_numbers);
  RUN_TEST(test_bools);
  return UNITY_END();
}
