// Host unit tests for the access point's name: pio test -e native
#include <unity.h>

#include "WifiNaming.h"

void setUp() {}
void tearDown() {}

static void test_name_from_address() {
  char out[33];
  const uint8_t mac[6] = {0xC8, 0xDA, 0x29, 0x0B, 0x2B, 0x90};
  WifiNaming::apSsid(out, sizeof(out), "EspBase", mac);
  TEST_ASSERT_EQUAL_STRING("EspBase-2B90", out);
}

static void test_long_prefix_is_cut_to_27() {
  char out[33];
  const uint8_t mac[6] = {0, 0, 0, 0, 0xAB, 0x01};
  WifiNaming::apSsid(out, sizeof(out), "0123456789012345678901234567890123456789", mac);
  TEST_ASSERT_EQUAL_STRING("012345678901234567890123456-AB01", out);  // 32 characters, the SSID limit
}

static void test_small_buffer_is_terminated() {
  char out[8];
  const uint8_t mac[6] = {0, 0, 0, 0, 0x12, 0x34};
  WifiNaming::apSsid(out, sizeof(out), "EspBase", mac);
  TEST_ASSERT_EQUAL_STRING("EspBase", out);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_name_from_address);
  RUN_TEST(test_long_prefix_is_cut_to_27);
  RUN_TEST(test_small_buffer_is_terminated);
  return UNITY_END();
}
