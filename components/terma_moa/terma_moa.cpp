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

#include <cstdlib>

namespace esphome {
namespace terma_moa {

static const char *const TAG = "terma_moa";

void TermaMoa::dump_config() {
  ESP_LOGCONFIG(TAG, "Terma MOA Blue: requesting link encryption automatically on connect");
}

void TermaMoa::setup() {
  // Diagnostic: re-read every Terma characteristic every 15s while connected, so the
  // PROBE lines show up reliably in the live log (not just during the reboot window).
  this->set_interval("terma_probe", 15000, [this]() {
    if (this->node_state == esp32_ble_tracker::ClientState::ESTABLISHED &&
        !this->probe_handles_.empty()) {
      this->probe_idx_ = 0;
      this->probe_next_();
    }
  });
}

void TermaMoa::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                   esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_OPEN_EVT:
      // Remember the connection so the periodic probe can read characteristics.
      this->gattc_if_ = gattc_if;
      this->conn_id_ = param->open.conn_id;
      // The parent BLEClient only forwards events for our own connection, and it
      // has already verified the peer address, so param->open.remote_bda is the
      // radiator's address. Request encryption the instant the link opens.
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
      // built-in ble_client nodes. Without this the parent's all_nodes_established_
      // check never passes, so it never releases the GATT service cache. The parent
      // resets node_state on disconnect, so no reset is needed here.
      this->node_state = esp32_ble_tracker::ClientState::ESTABLISHED;
      if (!this->gatt_dumped_) {
        this->gatt_dumped_ = true;
        this->dump_gatt_(gattc_if, param->search_cmpl.conn_id);
      }
      break;
    case ESP_GATTC_READ_CHAR_EVT:
      // Diagnostic probe: log the value of each Terma characteristic we requested.
      if (this->probe_idx_ < this->probe_handles_.size() &&
          param->read.handle == this->probe_handles_[this->probe_idx_]) {
        if (param->read.status == ESP_GATT_OK) {
          ESP_LOGD(TAG, "PROBE handle=0x%04X (%d B): %s", param->read.handle,
                   param->read.value_len,
                   format_hex_pretty(param->read.value, param->read.value_len).c_str());
        } else {
          ESP_LOGD(TAG, "PROBE handle=0x%04X read status=%d", param->read.handle,
                   param->read.status);
        }
        this->probe_idx_++;
        this->probe_next_();
      }
      break;
    default:
      break;
  }
}

void TermaMoa::dump_gatt_(esp_gatt_if_t gattc_if, uint16_t conn_id) {
  uint16_t count = 0;
  esp_gatt_status_t st = esp_ble_gattc_get_attr_count(
      gattc_if, conn_id, ESP_GATT_DB_CHARACTERISTIC, 0x0001, 0xFFFF, 0, &count);
  if (st != ESP_GATT_OK || count == 0) {
    ESP_LOGW(TAG, "GATT dump: attr count failed (status=%d count=%u)", st, count);
    return;
  }
  esp_gattc_char_elem_t *chars =
      (esp_gattc_char_elem_t *) malloc(sizeof(esp_gattc_char_elem_t) * count);
  if (chars == nullptr) {
    ESP_LOGW(TAG, "GATT dump: alloc failed for %u chars", count);
    return;
  }
  uint16_t got = count;
  this->probe_handles_.clear();
  if (esp_ble_gattc_get_all_char(gattc_if, conn_id, 0x0001, 0xFFFF, chars, &got, 0) ==
      ESP_GATT_OK) {
    ESP_LOGD(TAG, "GATT dump: %u characteristics", got);
    for (uint16_t i = 0; i < got; i++) {
      auto uuid = esp32_ble_tracker::ESPBTUUID::from_uuid(chars[i].uuid);
      // props bit0=broadcast 1=read 2=write-no-rsp 3=write 4=notify 5=indicate
      ESP_LOGD(TAG, "  handle=0x%04X props=0x%02X uuid=%s", chars[i].char_handle,
               chars[i].properties, uuid.to_string().c_str());
      // Queue readable Terma-service characteristics (handle >= 0x0015) for value probing.
      if ((chars[i].properties & ESP_GATT_CHAR_PROP_BIT_READ) && chars[i].char_handle >= 0x0015) {
        this->probe_handles_.push_back(chars[i].char_handle);
      }
    }
  } else {
    ESP_LOGW(TAG, "GATT dump: get_all_char failed");
  }
  free(chars);
  this->gattc_if_ = gattc_if;
  this->conn_id_ = conn_id;
  // The 15s interval in setup() drives the actual value reads.
}

void TermaMoa::probe_next_() {
  while (this->probe_idx_ < this->probe_handles_.size()) {
    uint16_t h = this->probe_handles_[this->probe_idx_];
    esp_err_t err =
        esp_ble_gattc_read_char(this->gattc_if_, this->conn_id_, h, ESP_GATT_AUTH_REQ_NONE);
    if (err == ESP_OK)
      return;  // wait for ESP_GATTC_READ_CHAR_EVT, which reads the next one
    ESP_LOGW(TAG, "PROBE handle=0x%04X read call failed: %s", h, esp_err_to_name(err));
    this->probe_idx_++;
  }
}

}  // namespace terma_moa
}  // namespace esphome

#endif  // USE_ESP32
