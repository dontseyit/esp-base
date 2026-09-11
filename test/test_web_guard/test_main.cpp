// Host unit tests for the request checks: pio test -e native
#include <unity.h>

#include "WebGuard.h"

void setUp() {}
void tearDown() {}

static size_t u8(const char* s) {
  return WebGuard::utf8Length(reinterpret_cast<const uint8_t*>(s));
}

static void test_utf8() {
  TEST_ASSERT_EQUAL(2, u8("\xC3\xA9"));          // e acute
  TEST_ASSERT_EQUAL(3, u8("\xE2\x82\xAC"));      // euro sign
  TEST_ASSERT_EQUAL(4, u8("\xF0\x9F\x98\x80"));  // emoji
  TEST_ASSERT_EQUAL(0, u8("\xC3"));              // truncated
  TEST_ASSERT_EQUAL(0, u8("\xC0\xAF"));          // overlong
  TEST_ASSERT_EQUAL(0, u8("\xED\xA0\x80"));      // surrogate
  TEST_ASSERT_EQUAL(0, u8("\xF4\x90\x80\x80"));  // beyond U+10FFFF
  TEST_ASSERT_EQUAL(0, u8("\x80"));              // stray continuation byte
  TEST_ASSERT_EQUAL(0, u8("\xFF"));
}

static void test_host() {
  TEST_ASSERT_TRUE(WebGuard::hostAllowed("192.168.4.1", "dev"));
  TEST_ASSERT_TRUE(WebGuard::hostAllowed("10.0.0.7:8080", "dev"));
  TEST_ASSERT_TRUE(WebGuard::hostAllowed("[fe80::1]:80", "dev"));
  TEST_ASSERT_TRUE(WebGuard::hostAllowed("dev.local", "dev"));
  TEST_ASSERT_TRUE(WebGuard::hostAllowed("DEV.LOCAL:80", "dev"));
  TEST_ASSERT_TRUE(WebGuard::hostAllowed("dev", "dev"));
  TEST_ASSERT_TRUE(WebGuard::hostAllowed(nullptr, "dev"));
  TEST_ASSERT_FALSE(WebGuard::hostAllowed("evil.com", "dev"));
  TEST_ASSERT_FALSE(WebGuard::hostAllowed("dev.local.evil.com", "dev"));
  TEST_ASSERT_FALSE(WebGuard::hostAllowed("devx.local", "dev"));
  TEST_ASSERT_FALSE(WebGuard::hostAllowed("1.2.3", "dev"));
  TEST_ASSERT_FALSE(WebGuard::hostAllowed("dev.local", ""));
}

static void test_origin() {
  TEST_ASSERT_TRUE(WebGuard::originMatches(nullptr, "dev.local"));
  TEST_ASSERT_TRUE(WebGuard::originMatches("http://dev.local", "dev.local"));
  TEST_ASSERT_TRUE(WebGuard::originMatches("http://192.168.1.5:8080", "192.168.1.5:8080"));
  TEST_ASSERT_FALSE(WebGuard::originMatches("http://evil.com", "dev.local"));
  TEST_ASSERT_FALSE(WebGuard::originMatches("http://dev.local:81", "dev.local"));
  TEST_ASSERT_FALSE(WebGuard::originMatches("null", "dev.local"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_utf8);
  RUN_TEST(test_host);
  RUN_TEST(test_origin);
  return UNITY_END();
}
