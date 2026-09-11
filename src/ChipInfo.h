// ChipInfo: the single place where chip specific preprocessor checks live.
// Everything else in the library asks these functions instead of testing
// CONFIG_IDF_TARGET_* or SOC_* macros directly.
#pragma once

#include <sdkconfig.h>
#include <soc/soc_caps.h>

namespace ChipInfo {

// Number of CPU cores (1 on ESP32-S2/C3/C6, 2 on ESP32/S3).
constexpr int cores() {
  return static_cast<int>(SOC_CPU_CORES_NUM);
}

// Bluetooth Low Energy controller present.
constexpr bool hasBle() {
#if defined(SOC_BLE_SUPPORTED) && SOC_BLE_SUPPORTED
  return true;
#else
  return false;
#endif
}

// A USB serial console is possible: either the USB Serial/JTAG peripheral
// (S3, C3, C6) or the native USB OTG controller (S2, S3).
constexpr bool hasUsbCdc() {
#if (defined(SOC_USB_SERIAL_JTAG_SUPPORTED) && SOC_USB_SERIAL_JTAG_SUPPORTED) || (defined(SOC_USB_OTG_SUPPORTED) && SOC_USB_OTG_SUPPORTED)
  return true;
#else
  return false;
#endif
}

// External PSRAM controller present on the die (does not mean a chip is fitted).
constexpr bool hasPsramSupport() {
#if defined(SOC_SPIRAM_SUPPORTED) && SOC_SPIRAM_SUPPORTED
  return true;
#else
  return false;
#endif
}

// IDF target name, e.g. "esp32", "esp32s3", "esp32c3".
inline const char* name() {
  return CONFIG_IDF_TARGET;
}

// Core the Arduino loop task runs on: 1 on dual core parts, 0 on single core.
// Use this instead of a literal when pinning tasks.
constexpr int appCore() {
  return cores() > 1 ? 1 : 0;
}

}  // namespace ChipInfo
