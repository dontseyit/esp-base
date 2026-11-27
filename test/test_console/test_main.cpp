// Host unit tests for the command parser and registry: pio test -e native
#include <unity.h>

#include "../../src/Console.cpp"

using namespace espbase;

void setUp() {}
void tearDown() {}

static void test_parse_tokens() {
  String cmd;
  CmdArgs a;
  TEST_ASSERT_TRUE(CmdArgs::parse("  wifi   set  Home  secret ", cmd, a));
  TEST_ASSERT_EQUAL_STRING("wifi", cmd.c_str());
  TEST_ASSERT_EQUAL(3, a.size());
  TEST_ASSERT_EQUAL_STRING("set", a[0].c_str());
  TEST_ASSERT_EQUAL_STRING("Home", a[1].c_str());
  TEST_ASSERT_EQUAL_STRING("secret", a[2].c_str());
  TEST_ASSERT_TRUE(a.is(0, "SET"));  // case insensitive
  TEST_ASSERT_FALSE(a.has(3));
  TEST_ASSERT_EQUAL_STRING("", a[7].c_str());  // out of range is empty, never a crash
}

static void test_parse_quotes() {
  String cmd;
  CmdArgs a;
  TEST_ASSERT_TRUE(CmdArgs::parse("wifi set \"My Home\" 'pa ss' esc\\aped \"a\\\"b\"", cmd, a));
  TEST_ASSERT_EQUAL(5, a.size());
  TEST_ASSERT_EQUAL_STRING("My Home", a[1].c_str());
  TEST_ASSERT_EQUAL_STRING("pa ss", a[2].c_str());
  TEST_ASSERT_EQUAL_STRING("esc\\aped", a[3].c_str());  // outside quotes a backslash is literal
  TEST_ASSERT_EQUAL_STRING("a\"b", a[4].c_str());       // inside quotes it escapes the next character
  TEST_ASSERT_TRUE(CmdArgs::parse("x \"unterminated", cmd, a));
  TEST_ASSERT_EQUAL_STRING("unterminated", a[0].c_str());
}

static void test_parse_limits() {
  String cmd;
  CmdArgs a;
  TEST_ASSERT_FALSE(CmdArgs::parse("   ", cmd, a));
  TEST_ASSERT_TRUE(CmdArgs::parse("c 1 2 3 4 5 6 7 8 9 10", cmd, a));
  TEST_ASSERT_EQUAL(CmdArgs::kMax, a.size());  // extra tokens are dropped
  TEST_ASSERT_EQUAL_STRING("8", a[7].c_str());
}

static void test_to_int() {
  String cmd;
  CmdArgs a;
  CmdArgs::parse("c 42 0x10 -7 abc", cmd, a);
  TEST_ASSERT_EQUAL(42, a.toInt(0));
  TEST_ASSERT_EQUAL(16, a.toInt(1));
  TEST_ASSERT_EQUAL(-7, a.toInt(2));
  TEST_ASSERT_EQUAL(99, a.toInt(3, 99));  // not a number: default
  TEST_ASSERT_EQUAL(5, a.toInt(9, 5));    // missing: default
}

static void test_execute_and_replace() {
  Console c;
  int calls = 0;
  c.addCommand(
      "led",
      [&](const CmdArgs& a, Print& out) {
        calls++;
        out.printf("led %s", a[0].c_str());
      },
      "led on|off");
  StringPrint out;
  TEST_ASSERT_TRUE(c.execute("LED on", out));  // command names are case insensitive
  TEST_ASSERT_EQUAL_STRING("led on", out.buf.c_str());
  TEST_ASSERT_EQUAL(1, calls);

  c.addCommand("led", [&](const CmdArgs&, Print& out) { out.print("new"); }, "");  // same name replaces
  out.buf = "";
  c.execute("led", out);
  TEST_ASSERT_EQUAL_STRING("new", out.buf.c_str());
  TEST_ASSERT_EQUAL(1, calls);

  out.buf = "";
  TEST_ASSERT_FALSE(c.execute("nope", out));
  TEST_ASSERT_EQUAL_STRING("unknown command \"nope\" (try: help)\r\n", out.buf.c_str());
  TEST_ASSERT_TRUE(c.execute("", out));  // an empty line is not an error
}

static void test_help_sorted() {
  Console c;
  c.addCommand("wifi", [](const CmdArgs&, Print&) {}, "w");
  c.addCommand("app", [](const CmdArgs&, Print&) {}, "a");
  c.addCommand("log", [](const CmdArgs&, Print&) {}, "l");
  StringPrint out;
  c.printHelp(out);
  TEST_ASSERT_EQUAL_STRING("  app            a\r\n  log            l\r\n  wifi           w\r\n", out.buf.c_str());
}

// A Stream fed from a string, as a serial port would be.
class FakeSerial : public Stream {
 public:
  explicit FakeSerial(const char* s) : _in(s) {}
  int available() override { return static_cast<int>(_in.size() - _pos); }
  int read() override { return _pos < _in.size() ? _in[_pos++] : -1; }
  size_t write(uint8_t c) override {
    out += static_cast<char>(c);
    return 1;
  }
  std::string out;

 private:
  std::string _in;
  size_t _pos = 0;
};

static void test_poll() {
  Console c;
  c.addCommand("echo", [](const CmdArgs& a, Print& out) { out.println(a[0]); }, "");
  FakeSerial io("echo one\r\n\n\recho two\nech");  // CRLF, blank lines and a partial line
  c.poll(io);
  TEST_ASSERT_EQUAL_STRING("one\r\ntwo\r\n", io.out.c_str());
  FakeSerial more("o three\n");  // the partial line continues on the next poll
  c.poll(more);
  TEST_ASSERT_EQUAL_STRING("three\r\n", more.out.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse_tokens);
  RUN_TEST(test_parse_quotes);
  RUN_TEST(test_parse_limits);
  RUN_TEST(test_to_int);
  RUN_TEST(test_execute_and_replace);
  RUN_TEST(test_help_sorted);
  RUN_TEST(test_poll);
  return UNITY_END();
}
