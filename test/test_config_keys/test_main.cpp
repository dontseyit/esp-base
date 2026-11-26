// Host tests for the library's key table (EspBaseKeys::Defs): pio test -e native
#include <unity.h>

#include "../../src/ConfigStore.cpp"
#include "../../src/Log.cpp"

using EspBaseKeys::Def;
using EspBaseKeys::Owner;
using EspBaseKeys::Type;

void setUp() {}
void tearDown() {}

// A slot key missing from the table could be taken by a project's setting,
// and WifiManager would read that setting as a stored network.
static void test_every_slot_is_listed() {
  char key[16];
  for (uint8_t slot = 1; slot <= EspBaseKeys::kMaxStaSlots; ++slot) {
    const Def* ssid = EspBaseKeys::find(EspBaseKeys::slotSsidKey(slot, key, sizeof(key)));
    TEST_ASSERT_NOT_NULL_MESSAGE(ssid, key);
    TEST_ASSERT_TRUE_MESSAGE(ssid->type == Type::Text && ssid->owner == Owner::Wifi, key);
    const Def* pass = EspBaseKeys::find(EspBaseKeys::slotPassKey(slot, key, sizeof(key)));
    TEST_ASSERT_NOT_NULL_MESSAGE(pass, key);
    TEST_ASSERT_TRUE_MESSAGE(pass->type == Type::Secret && pass->owner == Owner::Wifi, key);
  }
}

// Every name in EspBaseKeys has a row ("log level" looks log_lvl up and uses it).
static void test_every_named_key_is_listed() {
  const char* const names[] = {EspBaseKeys::Hostname,   EspBaseKeys::ApPass,     EspBaseKeys::WebPass,       EspBaseKeys::LogLevelKey,
                               EspBaseKeys::WifiModeKey, EspBaseKeys::StaTimeout, EspBaseKeys::ReconnTimeout, EspBaseKeys::ApRetry,
                               EspBaseKeys::BootApp};
  for (const char* name : names) {
    TEST_ASSERT_NOT_NULL_MESSAGE(EspBaseKeys::find(name), name);
  }
}

// config shows a Secret masked: every password key must be one.
static void test_every_password_is_secret() {
  for (const Def* d = EspBaseKeys::Defs; d->key != nullptr; ++d) {
    const size_t n = strlen(d->key);
    if (n >= 5 && strcmp(d->key + n - 5, "_pass") == 0) {
      TEST_ASSERT_TRUE_MESSAGE(d->type == Type::Secret, d->key);
    }
  }
}

static void test_keys_are_unique_nvs_keys() {
  for (const Def* d = EspBaseKeys::Defs; d->key != nullptr; ++d) {
    TEST_ASSERT_TRUE_MESSAGE(*d->key != '\0' && strlen(d->key) <= ConfigStore::kMaxKeyLen, d->key);
    TEST_ASSERT_EQUAL_PTR_MESSAGE(d, EspBaseKeys::find(d->key), d->key);  // the first match is this one
  }
}

static void test_other_keys_are_not_found() {
  TEST_ASSERT_NULL(EspBaseKeys::find("map_key"));
  TEST_ASSERT_NULL(EspBaseKeys::find("sta6_ssid"));
  TEST_ASSERT_NULL(EspBaseKeys::find(""));
  TEST_ASSERT_NULL(EspBaseKeys::find(nullptr));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_slot_is_listed);
  RUN_TEST(test_every_named_key_is_listed);
  RUN_TEST(test_every_password_is_secret);
  RUN_TEST(test_keys_are_unique_nvs_keys);
  RUN_TEST(test_other_keys_are_not_found);
  return UNITY_END();
}
