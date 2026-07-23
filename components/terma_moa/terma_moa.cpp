/*
 * AI-Provenance:
 *   model: claude-opus-4-8[1m]
 *   harness: Claude Code
 *   skills:
 *     - brainstorming
 */
#ifdef USE_ESP32

#include "terma_moa.h"
#include "esphome/core/log.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"

namespace esphome {
namespace terma_moa {

static const char *const TAG = "terma_moa";

void TermaMoa::dump_config() {
  ESP_LOGCONFIG(TAG, "Terma MOA Blue: requesting link encryption automatically on connect");
}

void TermaMoa::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                   esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_OPEN_EVT:
      // The parent BLEClient only forwards events for our own connection, and it has
      // already verified the peer address, so param->open.remote_bda is the radiator's
      // address. Request encryption the instant the link opens.
      if (param->open.status == ESP_GATT_OK) {
        esp_err_t err = esp_ble_set_encryption(param->open.remote_bda, ESP_BLE_SEC_ENCRYPT);
        if (err == ESP_OK) {
          ESP_LOGI(TAG, "Encryption requested for bonded device");
        } else {
          ESP_LOGW(TAG, "esp_ble_set_encryption failed: %s", esp_err_to_name(err));
        }
      }
      break;
    case ESP_GATTC_SEARCH_CMPL_EVT:
      // Mark this node established once service discovery finishes, matching the
      // built-in ble_client nodes, so the parent can release its GATT service cache.
      // The parent resets node_state on disconnect, so no reset is needed here.
      this->node_state = esp32_ble_tracker::ClientState::ESTABLISHED;
      break;
    default:
      break;
  }
}

}  // namespace terma_moa
}  // namespace esphome

#endif  // USE_ESP32
