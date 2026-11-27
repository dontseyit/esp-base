// Host unit tests for the intrusive app registry: pio test -e native
#include <unity.h>

#include "../../src/AppRegistry.cpp"

using namespace espbase;

namespace {
struct TestApp : App {
  explicit TestApp(const char* id, int16_t order) : App({.id = id, .name = id, .order = order}) {}
};

TestApp zed("zed", 10);
TestApp alpha("alpha", 10);
TestApp late("late", 900);
TestApp first("first", 0);
}  // namespace

void setUp() {}
void tearDown() {}

static void test_sorted_by_order_then_id() {
  // Registration order is reverse of what a launcher wants; the registry sorts on first read.
  AppRegistry::add(zed);
  AppRegistry::add(alpha);
  AppRegistry::add(late);
  AppRegistry::add(first);
  AppRegistry::add(alpha);  // double add is ignored
  TEST_ASSERT_EQUAL(4, AppRegistry::count());
  const char* expect[] = {"first", "alpha", "zed", "late"};
  size_t i = 0;
  for (App* a = AppRegistry::first(); a != nullptr; a = AppRegistry::next(*a), ++i) {
    TEST_ASSERT_TRUE(i < 4);
    TEST_ASSERT_EQUAL_STRING(expect[i], a->id());
  }
  TEST_ASSERT_EQUAL(4, i);
}

static void test_find() {
  TEST_ASSERT_EQUAL_PTR(&late, AppRegistry::find("late"));
  TEST_ASSERT_NULL(AppRegistry::find("nope"));
  TEST_ASSERT_NULL(AppRegistry::find(nullptr));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_sorted_by_order_then_id);
  RUN_TEST(test_find);
  return UNITY_END();
}
