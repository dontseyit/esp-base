// Host unit tests for the persisted key helpers: pio test -e native
#include <string.h>
#include <unity.h>

#include "../../src/Keys.cpp"

using namespace espbase;

void setUp() {}
void tearDown() {}

static void test_slot_keys() {
  char buf[Keys::kMaxKeyLen + 1];
  TEST_ASSERT_EQUAL_STRING("sta_ssid", Keys::slotSsidKey(1, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("sta_pass", Keys::slotPassKey(1, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("sta2_ssid", Keys::slotSsidKey(2, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("sta5_pass", Keys::slotPassKey(5, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("sta_ssid", Keys::slotSsidKey(0, buf, sizeof(buf)));  // 0 means slot 1
}

static void test_is_slot_key() {
  uint8_t slot = 0;
  bool pass = false;
  TEST_ASSERT_TRUE(Keys::isSlotKey("sta_ssid", slot, pass));
  TEST_ASSERT_EQUAL(1, slot);
  TEST_ASSERT_FALSE(pass);
  TEST_ASSERT_TRUE(Keys::isSlotKey("sta3_pass", slot, pass));
  TEST_ASSERT_EQUAL(3, slot);
  TEST_ASSERT_TRUE(pass);
  TEST_ASSERT_FALSE(Keys::isSlotKey("sta1_ssid", slot, pass));  // slot 1 has no number
  TEST_ASSERT_FALSE(Keys::isSlotKey("sta6_ssid", slot, pass));  // beyond kMaxStaSlots
  TEST_ASSERT_FALSE(Keys::isSlotKey("sta_timeout", slot, pass));
  TEST_ASSERT_FALSE(Keys::isSlotKey("hostname", slot, pass));
  TEST_ASSERT_FALSE(Keys::isSlotKey(nullptr, slot, pass));
}

static void test_is_secret() {
  TEST_ASSERT_TRUE(Keys::isSecret("sta_pass"));
  TEST_ASSERT_TRUE(Keys::isSecret("web_pass"));
  TEST_ASSERT_TRUE(Keys::isSecret("sta4_pass"));
  TEST_ASSERT_FALSE(Keys::isSecret("sta_ssid"));
  TEST_ASSERT_FALSE(Keys::isSecret("pass"));
  TEST_ASSERT_FALSE(Keys::isSecret(nullptr));
}

static void test_all_keys_fit_nvs() {
  size_t n = 0;
  for (const char* const* k = Keys::All; *k != nullptr; ++k, ++n) {
    TEST_ASSERT_TRUE_MESSAGE(strlen(*k) <= Keys::kMaxKeyLen, *k);
    for (const char* const* o = k + 1; *o != nullptr; ++o) {
      TEST_ASSERT_TRUE_MESSAGE(strcmp(*k, *o) != 0, *k);  // no duplicates
    }
  }
  TEST_ASSERT_EQUAL(10 + 9, n);  // 5 slots x 2 fields, plus the 9 single keys
  TEST_ASSERT_TRUE(strlen(Keys::Namespace) <= Keys::kMaxKeyLen);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_slot_keys);
  RUN_TEST(test_is_slot_key);
  RUN_TEST(test_is_secret);
  RUN_TEST(test_all_keys_fit_nvs);
  return UNITY_END();
}
