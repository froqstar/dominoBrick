#include "ble_server.h"

#include "cat.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>

static const char *TAG = "ble";
static const char *DEV_NAME = "dominoBrick";

static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t conn_mtu = BLE_ATT_MTU_DFLT;
static uint8_t own_addr_type;

static uint16_t h_tx;
static uint16_t h_prog;
static uint16_t h_sec;
static uint16_t h_rxpri;
static uint16_t h_rxsec;
static uint16_t h_freq;

#define RX_CACHE_MAX 240

static portMUX_TYPE cache_mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t prog_last;
static uint8_t rx_pri_cache[RX_CACHE_MAX];
static uint16_t rx_pri_len;
static uint8_t rx_sec_cache[RX_CACHE_MAX];
static uint16_t rx_sec_len;

static const ble_uuid128_t svc_uuid =
    BLE_UUID128_INIT(0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                     0x88, 0x99, 0xaa, 0xbb, 0x00, 0x00, 0xb1, 0xd0);
static const ble_uuid128_t tx_uuid =
    BLE_UUID128_INIT(0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                     0x88, 0x99, 0xaa, 0xbb, 0x01, 0x00, 0xb1, 0xd0);
static const ble_uuid128_t prog_uuid =
    BLE_UUID128_INIT(0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                     0x88, 0x99, 0xaa, 0xbb, 0x02, 0x00, 0xb1, 0xd0);
static const ble_uuid128_t sec_uuid =
    BLE_UUID128_INIT(0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                     0x88, 0x99, 0xaa, 0xbb, 0x03, 0x00, 0xb1, 0xd0);
static const ble_uuid128_t rxpri_uuid =
    BLE_UUID128_INIT(0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                     0x88, 0x99, 0xaa, 0xbb, 0x04, 0x00, 0xb1, 0xd0);
static const ble_uuid128_t rxsec_uuid =
    BLE_UUID128_INIT(0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                     0x88, 0x99, 0xaa, 0xbb, 0x05, 0x00, 0xb1, 0xd0);
static const ble_uuid128_t freq_uuid =
    BLE_UUID128_INIT(0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                     0x88, 0x99, 0xaa, 0xbb, 0x06, 0x00, 0xb1, 0xd0);

static int gatt_access(uint16_t ch, uint16_t attr,
                       struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gap_event(struct ble_gap_event *event, void *arg);

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &tx_uuid.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &h_tx,
            },
            {
                .uuid = &prog_uuid.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &h_prog,
            },
            {
                .uuid = &sec_uuid.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &h_sec,
            },
            {
                .uuid = &rxpri_uuid.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &h_rxpri,
            },
            {
                .uuid = &rxsec_uuid.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &h_rxsec,
            },
            {
                .uuid = &freq_uuid.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &h_freq,
            },
            {
                0,
            },
        },
    },
    {
        0,
    },
};

static void advertise(void)
{
    struct ble_hs_adv_fields fields;
    struct ble_hs_adv_fields rsp;
    struct ble_gap_adv_params adv_params;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = &svc_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields rc=%d", rc);
        return;
    }
    memset(&rsp, 0, sizeof(rsp));
    rsp.name = (uint8_t *)ble_svc_gap_device_name();
    rsp.name_len = strlen(ble_svc_gap_device_name());
    rsp.name_is_complete = 1;
    rsp.tx_pwr_lvl_is_present = 1;
    rsp.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        ESP_LOGE(TAG, "rsp_set_fields rc=%d", rc);
        return;
    }
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_start rc=%d", rc);
    }
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            conn_handle = event->connect.conn_handle;
            conn_mtu = BLE_ATT_MTU_DFLT;
            ESP_LOGI(TAG, "connect h=%d", conn_handle);
            struct ble_gap_upd_params upd;
            memset(&upd, 0, sizeof(upd));
            upd.itvl_min = 12;
            upd.itvl_max = 24;
            upd.latency = 0;
            upd.supervision_timeout = 400;
            upd.min_ce_len = 16;
            upd.max_ce_len = 16;
            int urc = ble_gap_update_params(conn_handle, &upd);
            if (urc != 0) {
                ESP_LOGW(TAG, "conn update rc=%d", urc);
            }
        } else {
            advertise();
        }
        break;
    case BLE_GAP_EVENT_CONN_UPDATE:
        if (event->conn_update.status == 0) {
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->conn_update.conn_handle, &desc) == 0) {
                ESP_LOGI(TAG, "conn update itvl=%dms latency=%d timeout=%dms",
                         (desc.conn_itvl * 5) / 4, desc.conn_latency,
                         desc.supervision_timeout * 10);
            }
        } else {
            ESP_LOGW(TAG, "conn update failed status=%d",
                     event->conn_update.status);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect reason=%d", event->disconnect.reason);
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        conn_mtu = BLE_ATT_MTU_DFLT;
        advertise();
        break;
    case BLE_GAP_EVENT_MTU:
        conn_mtu = event->mtu.value;
        ESP_LOGI(TAG, "mtu=%d", conn_mtu);
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "subscribe attr=%d cur_notify=%d",
                 event->subscribe.attr_handle,
                 event->subscribe.cur_notify);
        break;
    case BLE_GAP_EVENT_NOTIFY_TX:
        if (event->notify_tx.status != 0) {
            ESP_LOGW(TAG, "notify_tx failed status=%d attr=%d",
                     event->notify_tx.status,
                     event->notify_tx.attr_handle);
        }
        break;
    default:
        break;
    }
    return 0;
}

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "infer_auto rc=%d", rc);
        return;
    }
    advertise();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "reset reason=%d", reason);
}

static int gatt_access(uint16_t ch, uint16_t attr,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch;
    (void)arg;
    static uint8_t wbuf[512];
    uint16_t len = 0;
    int rc;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (attr == h_freq) {
            uint32_t hz = cat_freq_get();
            uint8_t le[4];
            le[0] = (uint8_t)(hz & 0xFF);
            le[1] = (uint8_t)((hz >> 8) & 0xFF);
            le[2] = (uint8_t)((hz >> 16) & 0xFF);
            le[3] = (uint8_t)((hz >> 24) & 0xFF);
            rc = os_mbuf_append(ctxt->om, le, 4);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        if (attr == h_prog) {
            taskENTER_CRITICAL(&cache_mux);
            uint8_t c = prog_last;
            taskEXIT_CRITICAL(&cache_mux);
            rc = os_mbuf_append(ctxt->om, &c, 1);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        if (attr == h_rxpri || attr == h_rxsec) {
            taskENTER_CRITICAL(&cache_mux);
            const uint8_t *src = attr == h_rxpri ? rx_pri_cache : rx_sec_cache;
            uint16_t n = attr == h_rxpri ? rx_pri_len : rx_sec_len;
            if (n > sizeof(wbuf)) {
                n = sizeof(wbuf);
            }
            memcpy(wbuf, src, n);
            taskEXIT_CRITICAL(&cache_mux);
            if (n == 0) {
                return 0;
            }
            rc = os_mbuf_append(ctxt->om, wbuf, n);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        uint16_t n = domino_ble_sec_get(wbuf, sizeof(wbuf));
        if (n == 0) {
            return 0;
        }
        rc = os_mbuf_append(ctxt->om, wbuf, n);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    rc = ble_hs_mbuf_to_flat(ctxt->om, wbuf, sizeof(wbuf), &len);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (attr == h_sec) {
        domino_ble_sec_set(wbuf, len);
        return 0;
    }
    if (attr == h_freq) {
        if (len != 4) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        uint32_t hz = (uint32_t)wbuf[0] | ((uint32_t)wbuf[1] << 8) |
                      ((uint32_t)wbuf[2] << 16) | ((uint32_t)wbuf[3] << 24);
        if (hz == 0 || hz > 450000000u) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        if (cat_request_freq(hz) != 0) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return 0;
    }
    if (attr != h_tx) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (domino_ble_tx_append(wbuf, len) != 0) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
}

static void notify_handle(uint16_t handle, const uint8_t *data, uint16_t len)
{
    if (conn_handle == BLE_HS_CONN_HANDLE_NONE || len == 0) {
        return;
    }
    uint16_t chunk = conn_mtu > 3 ? conn_mtu - 3 : 20;
    for (uint16_t off = 0; off < len; off += chunk) {
        uint16_t n = len - off > chunk ? chunk : len - off;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(data + off, n);
        if (om == NULL) {
            ESP_LOGW(TAG, "mbuf alloc failed");
            return;
        }
        int rc = ble_gatts_notify_custom(conn_handle, handle, om);
        if (rc != 0) {
            ESP_LOGW(TAG, "notify rc=%d", rc);
            return;
        }
    }
}

int ble_connected(void)
{
    return conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

void ble_notify_progress(uint8_t c)
{
    taskENTER_CRITICAL(&cache_mux);
    prog_last = c;
    taskEXIT_CRITICAL(&cache_mux);
    notify_handle(h_prog, &c, 1);
}

void ble_notify_rx_primary(const uint8_t *data, uint16_t len)
{
    if (len == 0) {
        return;
    }
    taskENTER_CRITICAL(&cache_mux);
    rx_pri_len = len > RX_CACHE_MAX ? RX_CACHE_MAX : len;
    memcpy(rx_pri_cache, data, rx_pri_len);
    taskEXIT_CRITICAL(&cache_mux);
    notify_handle(h_rxpri, data, len);
}

void ble_notify_rx_secondary(const uint8_t *data, uint16_t len)
{
    if (len == 0) {
        return;
    }
    taskENTER_CRITICAL(&cache_mux);
    rx_sec_len = len > RX_CACHE_MAX ? RX_CACHE_MAX : len;
    memcpy(rx_sec_cache, data, rx_sec_len);
    taskEXIT_CRITICAL(&cache_mux);
    notify_handle(h_rxsec, data, len);
}

void ble_notify_freq(uint32_t hz)
{
    uint8_t le[4];
    le[0] = (uint8_t)(hz & 0xFF);
    le[1] = (uint8_t)((hz >> 8) & 0xFF);
    le[2] = (uint8_t)((hz >> 16) & 0xFF);
    le[3] = (uint8_t)((hz >> 24) & 0xFF);
    notify_handle(h_freq, le, 4);
}

uint8_t domino_ble_progress_get(void)
{
    taskENTER_CRITICAL(&cache_mux);
    uint8_t c = prog_last;
    taskEXIT_CRITICAL(&cache_mux);
    return c;
}

uint16_t domino_ble_rx_cache_get(int secondary, uint8_t *dst, uint16_t max)
{
    taskENTER_CRITICAL(&cache_mux);
    const uint8_t *src = secondary ? rx_sec_cache : rx_pri_cache;
    uint16_t n = secondary ? rx_sec_len : rx_pri_len;
    if (n > max) {
        n = max;
    }
    memcpy(dst, src, n);
    taskEXIT_CRITICAL(&cache_mux);
    return n;
}

static void ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    vTaskDelete(NULL);
}

void ble_server_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(nimble_port_init());

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "count_cfg rc=%d", rc);
        return;
    }
    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "add_svcs rc=%d", rc);
        return;
    }
    rc = ble_svc_gap_device_name_set(DEV_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "device_name rc=%d", rc);
        return;
    }
    xTaskCreatePinnedToCore(ble_host_task, "ble_host", 4096, NULL, 5, NULL, 0);
}
