// Host unit tests for the log ring buffer: pio test -e native
#include <unity.h>

#include "../../src/Log.cpp"

using namespace espbase;

namespace {
class Capture : public Print {
 public:
  size_t write(uint8_t c) override {
    out += static_cast<char>(c);
    return 1;
  }
  std::string out;
};

// Reads everything from `c` and returns the messages joined by '|'.
std::string drain(LogClass::Cursor& c, uint32_t* dropped = nullptr) {
  std::string all;
  LogClass::Record r;
  uint32_t d = 0;
  uint32_t total = 0;
  while (Log.read(c, r, &d)) {
    total += d;
    if (!all.empty())
      all += '|';
    all += r.msg;
  }
  if (dropped)
    *dropped = total;
  return all;
}
}  // namespace

void setUp() {
  // A same-size begin() keeps the records by design, so resize twice to start every test empty.
  Log.begin(LogClass::kMinRing * 2);
  Log.begin(LogClass::kMinRing);  // 512 bytes: a handful of records
  Log.setLevel(LogLevel::Debug);
  Log.setOutput(nullptr);
  shimMillis() = 0;
}
void tearDown() {}

static void test_prefix_and_sink() {
  Capture sink;
  Log.setOutput(&sink);
  shimMillis() = 12345;
  LOG_I("hello %d", 7);
  TEST_ASSERT_EQUAL_STRING("[   12345][I] hello 7\r\n", sink.out.c_str());
  LogClass::Cursor c = Log.tail();
  LogClass::Record r;
  TEST_ASSERT_TRUE(Log.read(c, r));
  TEST_ASSERT_EQUAL(12345, r.ms);
  TEST_ASSERT_EQUAL(static_cast<int>(LogLevel::Info), static_cast<int>(r.level));
  TEST_ASSERT_EQUAL_STRING("hello 7", r.msg);
  TEST_ASSERT_FALSE(Log.read(c, r));  // nothing more
}

static void test_level_gate() {
  Log.setLevel(LogLevel::Warn);
  LOG_I("dropped");
  LOG_D("dropped");
  LOG_W("kept");
  LOG_E("kept too");
  LogClass::Cursor c = Log.tail();
  TEST_ASSERT_EQUAL_STRING("kept|kept too", drain(c).c_str());
  Log.setLevel(LogLevel::None);
  LOG_E("silent");
  TEST_ASSERT_EQUAL_STRING("", drain(c).c_str());
}

static void test_control_chars_and_newlines() {
  LOG_I("a\tb\x1b[31m\n");
  LogClass::Cursor c = Log.tail();
  TEST_ASSERT_EQUAL_STRING("a?b?[31m", drain(c).c_str());  // tabs and escapes replaced, trailing newline removed
}

static void test_truncation() {
  char big[400];
  memset(big, 'x', sizeof(big) - 1);
  big[sizeof(big) - 1] = '\0';
  LOG_I("%s", big);
  LogClass::Cursor c = Log.tail();
  LogClass::Record r;
  TEST_ASSERT_TRUE(Log.read(c, r));
  TEST_ASSERT_EQUAL(LogClass::kMaxMessage, r.len);
  TEST_ASSERT_EQUAL_STRING("...", r.msg + LogClass::kMaxMessage - 3);
}

static void test_wraparound_drops_oldest() {
  // 512 byte ring, 7 byte header + 37 byte message = 44 bytes per record: 11 fit, 30 written.
  LogClass::Cursor early = Log.tail();
  for (int i = 0; i < 30; ++i) {
    LOG_I("record %02d ---------------------------", i);
  }
  uint32_t dropped = 0;
  const std::string got = drain(early, &dropped);
  TEST_ASSERT_EQUAL(30 - 11, dropped);                            // the reader fell behind and was told by exactly how much
  TEST_ASSERT_TRUE(got.rfind("record 29") != std::string::npos);  // the newest survives
  TEST_ASSERT_TRUE(got.find("record 00") == std::string::npos);   // the oldest is gone
  // A fresh reader sees a consistent, ordered tail.
  LogClass::Cursor c = Log.tail();
  LogClass::Record r;
  int prev = -1;
  while (Log.read(c, r)) {
    const int n = atoi(r.msg + 7);
    TEST_ASSERT_TRUE(n > prev);
    prev = n;
  }
  TEST_ASSERT_EQUAL(29, prev);
}

static void test_begin_keeps_messages_when_size_unchanged() {
  LOG_I("before");
  Log.begin(LogClass::kMinRing);  // same size: nothing is lost
  LogClass::Cursor c = Log.tail();
  TEST_ASSERT_EQUAL_STRING("before", drain(c).c_str());
  Log.begin(LogClass::kMinRing * 2);  // resize: the ring starts over
  c = Log.tail();
  TEST_ASSERT_EQUAL_STRING("", drain(c).c_str());
  TEST_ASSERT_EQUAL(LogClass::kMinRing * 2, Log.ringBytes());
}

static void test_dump() {
  shimMillis() = 5;
  LOG_W("one");
  LOG_E("two");
  Capture out;
  Log.dump(out);
  TEST_ASSERT_EQUAL_STRING("[       5][W] one\r\n[       5][E] two\r\n", out.out.c_str());
}

static void test_parse_level() {
  LogLevel l;
  TEST_ASSERT_TRUE(LogClass::parseLevel("debug", l));
  TEST_ASSERT_EQUAL(static_cast<int>(LogLevel::Debug), static_cast<int>(l));
  TEST_ASSERT_TRUE(LogClass::parseLevel("W", l));
  TEST_ASSERT_EQUAL(static_cast<int>(LogLevel::Warn), static_cast<int>(l));
  TEST_ASSERT_TRUE(LogClass::parseLevel("0", l));
  TEST_ASSERT_EQUAL(static_cast<int>(LogLevel::None), static_cast<int>(l));
  TEST_ASSERT_FALSE(LogClass::parseLevel("x", l));
  TEST_ASSERT_FALSE(LogClass::parseLevel("", l));
  TEST_ASSERT_FALSE(LogClass::parseLevel(nullptr, l));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_prefix_and_sink);
  RUN_TEST(test_level_gate);
  RUN_TEST(test_control_chars_and_newlines);
  RUN_TEST(test_truncation);
  RUN_TEST(test_wraparound_drops_oldest);
  RUN_TEST(test_begin_keeps_messages_when_size_unchanged);
  RUN_TEST(test_dump);
  RUN_TEST(test_parse_level);
  return UNITY_END();
}
