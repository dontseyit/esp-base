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

static void test_guarded_paths() {
  TEST_ASSERT_TRUE(WebGuard::appPath("/app"));
  TEST_ASSERT_TRUE(WebGuard::appPath("/app/"));
  TEST_ASSERT_TRUE(WebGuard::appPath("/app/index.html"));
  TEST_ASSERT_FALSE(WebGuard::appPath("/apps"));
  TEST_ASSERT_FALSE(WebGuard::appPath("/application"));
  TEST_ASSERT_FALSE(WebGuard::appPath("/dashboard"));
  TEST_ASSERT_FALSE(WebGuard::appPath(nullptr));

  TEST_ASSERT_TRUE(WebGuard::guardedPath("/api/info"));
  TEST_ASSERT_TRUE(WebGuard::guardedPath("/ws"));
  TEST_ASSERT_TRUE(WebGuard::guardedPath("/update"));
  TEST_ASSERT_TRUE(WebGuard::guardedPath("/app/"));
  TEST_ASSERT_TRUE(WebGuard::guardedPath("/app/data.json"));
  TEST_ASSERT_FALSE(WebGuard::guardedPath("/"));  // the page: login only, see WebConsole
  TEST_ASSERT_FALSE(WebGuard::guardedPath("/api"));
  TEST_ASSERT_FALSE(WebGuard::guardedPath("/apps"));
  TEST_ASSERT_FALSE(WebGuard::guardedPath("/generate_204"));
}

static void test_under_path() {
  TEST_ASSERT_TRUE(WebGuard::underPath("/console", "/console"));
  TEST_ASSERT_TRUE(WebGuard::underPath("/console/", "/console"));
  TEST_ASSERT_TRUE(WebGuard::underPath("/console/x", "/console"));  // ESPAsyncWebServer serves the page here too
  TEST_ASSERT_FALSE(WebGuard::underPath("/consoles", "/console"));
  TEST_ASSERT_FALSE(WebGuard::underPath("/", "/console"));
  TEST_ASSERT_TRUE(WebGuard::underPath("/", "/"));
  TEST_ASSERT_FALSE(WebGuard::underPath("/x", "/"));
  TEST_ASSERT_FALSE(WebGuard::underPath("/", ""));  // no console page: nothing is under it
  TEST_ASSERT_FALSE(WebGuard::underPath(nullptr, "/"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_utf8);
  RUN_TEST(test_host);
  RUN_TEST(test_origin);
  RUN_TEST(test_guarded_paths);
  RUN_TEST(test_under_path);
  return UNITY_END();
}
