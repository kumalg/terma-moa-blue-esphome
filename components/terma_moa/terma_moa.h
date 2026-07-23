/*
 * AI-Provenance:
 *   model: claude-opus-4-8[1m]
 *   harness: Claude Code
 *   skills:
 *     - brainstorming
 */
#pragma once

#ifdef USE_ESP32

#include "esphome/core/component.h"
#include "esphome/components/ble_client/ble_client.h"

#include <esp_gap_ble_api.h>
#include <esp_gattc_api.h>

namespace esphome {
namespace terma_moa {

// Registers as a node on a `ble_client` and, the moment the GATT connection
// opens, proactively requests link encryption. The Terma MOA Blue refuses to
// serve its characteristics over an unencrypted link (reads fail with
// insufficient-authentication), and ESPHome never initiates encryption on its
// own, so we do it here.
class TermaMoa : public Component, public ble_client::BLEClientNode {
 public:
  void dump_config() override;
  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;
};

}  // namespace terma_moa
}  // namespace esphome

#endif  // USE_ESP32
