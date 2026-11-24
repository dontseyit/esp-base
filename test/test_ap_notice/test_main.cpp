// Host unit tests for when the access point is announced: pio test -e native
#include <unity.h>

#include "ApNotice.h"

void setUp() {}
void tearDown() {}

static void test_one_start_is_one_notice() {
  ApNotice n;
  TEST_ASSERT_FALSE(n.due());
  n.started();
  TEST_ASSERT_TRUE(n.due());
  TEST_ASSERT_FALSE(n.due());
}

// Hosted WiFi restarts the AP while softAP() configures it: start, stop, start.
static void test_a_restart_by_the_driver_is_one_notice() {
  ApNotice onePass;  // all three events handled before the check
  onePass.started();
  onePass.stopped();
  onePass.started();
  TEST_ASSERT_TRUE(onePass.due());
  TEST_ASSERT_FALSE(onePass.due());

  ApNotice threePasses;  // a check after every event
  threePasses.started();
  TEST_ASSERT_TRUE(threePasses.due());
  threePasses.stopped();
  TEST_ASSERT_FALSE(threePasses.due());
  threePasses.started();
  TEST_ASSERT_FALSE(threePasses.due());

  ApNotice split;  // start and stop in one pass, the second start in the next
  split.started();
  split.stopped();
  TEST_ASSERT_FALSE(split.due());
  split.started();
  TEST_ASSERT_TRUE(split.due());
  TEST_ASSERT_FALSE(split.due());
}

static void test_taken_down_by_the_library_then_up_is_a_new_notice() {
  ApNotice n;
  n.started();
  TEST_ASSERT_TRUE(n.due());
  n.takenDown();
  TEST_ASSERT_FALSE(n.due());
  n.started();
  TEST_ASSERT_TRUE(n.due());
}

// Radio off and on again with no check in between (loop() returns early while off).
static void test_off_and_on_without_a_check_between_still_notifies() {
  ApNotice n;
  n.started();
  TEST_ASSERT_TRUE(n.due());
  n.takenDown();
  n.started();
  TEST_ASSERT_TRUE(n.due());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_one_start_is_one_notice);
  RUN_TEST(test_a_restart_by_the_driver_is_one_notice);
  RUN_TEST(test_taken_down_by_the_library_then_up_is_a_new_notice);
  RUN_TEST(test_off_and_on_without_a_check_between_still_notifies);
  return UNITY_END();
}
