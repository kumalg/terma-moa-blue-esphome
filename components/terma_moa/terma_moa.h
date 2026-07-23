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

#include <vector>

namespace esphome {
namespace terma_moa {

// Registers as a node on a `ble_client` and, the moment the GATT connection
// opens, proactively requests link encryption. The Terma MOA Blue refuses to
// serve its characteristics over an unencrypted link (reads fail with
// insufficient-authentication), and ESPHome never initiates encryption on its
// own, so we do it here.
class TermaMoa : public Component, public ble_client::BLEClientNode {
 public:
  void setup() override;
  void dump_config() override;
  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

 protected:
  // Diagnostic: once per boot, log every GATT characteristic (UUID/handle/props)
  // and then read + log the value of each Terma characteristic, to reverse-engineer
  // where the timer duration/countdown lives. Remove when done.
  void dump_gatt_(esp_gatt_if_t gattc_if, uint16_t conn_id);
  void probe_next_();
  bool gatt_dumped_{false};
  std::vector<uint16_t> probe_handles_;
  size_t probe_idx_{0};
  esp_gatt_if_t gattc_if_{0};  // current connection params
  uint16_t conn_id_{0};
};

}  // namespace terma_moa
}  // namespace esphome

#endif  // USE_ESP32
