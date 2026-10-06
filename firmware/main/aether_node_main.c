/*
 * aether_node_main.c
 * -------------------
 * Firmware generic untuk 1 node AETHER RTI network.
 * Semua node pakai kode yang SAMA - yang beda cuma NODE_ID (lihat aether_config.h).
 *
 * Cara kerja:
 *   1. Semua node selalu "mendengarkan" (CSI RX callback selalu aktif).
 *   2. Giliran kirim diatur token round-robin: node kirim beberapa paket,
 *      lalu di paket terakhirnya menitipkan "next_token" = NODE_ID berikutnya.
 *   3. Node lain yang menerima token untuk dirinya akan mulai kirim setelah
 *      GUARD_INTERVAL_MS, supaya tidak tabrakan dengan node yang baru selesai.
 *   4. Setiap CSI yang diterima dari MAC node lain di-print ke serial dalam
 *      format yang bisa diparse csi_logger_v2.py, ditandai sender_id + receiver_id
 *      (link_id) supaya backend tahu ini data link yang mana.
 *
 * STATUS: skeleton referensi. Struct wifi_csi_config_t / wifi_csi_info_t bisa
 * beda field tergantung versi ESP-IDF kamu - cek esp_wifi_types.h di IDF yang
 * ter-install (idf.py --version) dan sesuaikan kalau ada mismatch saat compile.
 * Target: ESP-IDF v5.x pada ESP32-S3.
 */

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "aether_config.h"

static const char *TAG = "AETHER_NODE";

// Payload paket ESP-NOW yang kita kirim antar node.
// Isinya minimal: siapa pengirim, nomor urut, dan siapa giliran berikutnya.
typedef struct __attribute__((packed)) {
    uint8_t  sender_id;
    uint32_t seq_num;
    uint8_t  next_token;   // NODE_ID yang boleh kirim setelah ini
} aether_packet_t;

static uint32_t s_seq_counter = 0;
static bool     s_my_turn_pending = false;

// ------------------------------------------------------------------
// Helper: cari NODE_ID dari MAC address yang dikenal (untuk tag link_id)
// ------------------------------------------------------------------
static int find_node_id_by_mac(const uint8_t *mac)
{
    for (int i = 0; i < TOTAL_NODES; i++) {
        if (memcmp(mac, NODE_MAC_TABLE[i], 6) == 0) {
            return i;
        }
    }
    return -1;  // MAC tidak dikenal -> bukan node kita, harus di-drop
}

// ------------------------------------------------------------------
// DIAGNOSTIK: callback promiscuous mentah, level lebih dasar dari CSI.
// Kalau ini juga tidak pernah terpanggil, masalahnya bukan di CSI,
// tapi di jalur penerimaan paket level bawah. Kalau ini terpanggil
// TAPI csi_rx_callback tidak, berarti murni masalah konfigurasi CSI.
// ------------------------------------------------------------------
static uint32_t s_promisc_pkt_count = 0;

static void promiscuous_rx_callback(void *buf, wifi_promiscuous_pkt_type_t type)
{
    s_promisc_pkt_count++;
    if (s_promisc_pkt_count % 50 == 1) {  // jangan flood serial, print tiap 50 paket
        ESP_LOGI(TAG, "[DIAG] promiscuous_rx_callback terpanggil, total=%lu, type=%d",
                 (unsigned long)s_promisc_pkt_count, (int)type);
    }
}

// ------------------------------------------------------------------
// CSI RX callback - dipanggil tiap kali radio menerima frame APA PUN.
// Kita filter: hanya proses kalau frame-nya dari salah satu node kita.
// ------------------------------------------------------------------
static void csi_rx_callback(void *ctx, wifi_csi_info_t *info)
{
    if (!info || !info->buf) {
        return;
    }

    // DIAGNOSTIK: pastikan callback CSI benar-benar terpanggil.
    ESP_LOGI(TAG, "[CSI RAW] MAC=%02X:%02X:%02X:%02X:%02X:%02X len=%d rssi=%d",
             info->mac[0], info->mac[1], info->mac[2],
             info->mac[3], info->mac[4], info->mac[5],
             info->len,
             info->rx_ctrl.rssi);

    int sender_id = find_node_id_by_mac(info->mac);
    if (sender_id < 0) {
        return;  // bukan dari node AETHER kita, abaikan (ambient traffic)
    }
    if (sender_id == NODE_ID) {
        return;  // paket dari diri sendiri (harusnya tidak terjadi, jaga-jaga)
    }

    // link_id sederhana: "sender->receiver" supaya backend bisa bedakan arah
    // Cetak dalam format CSV-friendly satu baris, mirip gaya ESP32-CSI-Tool
    // supaya csi_logger_v2.py di backend gampang parse.
    printf("%s,link=%d-%d,rssi=%d,rate=%d,len=%d,csi=[",
           LOG_TAG_CSI,
           sender_id, NODE_ID,
           info->rx_ctrl.rssi,
           info->rx_ctrl.rate,
           info->len);

    for (int i = 0; i < info->len; i++) {
        printf("%d ", info->buf[i]);
    }
    printf("]\n");
}

// ------------------------------------------------------------------
// ESP-NOW receive callback - untuk baca payload aether_packet_t
// (token round-robin), TERPISAH dari CSI callback di atas.
// Signature ini untuk ESP-IDF v5.x (esp_now_recv_info_t).
// Kalau IDF kamu versi lama (v4.x), signature-nya
// (const uint8_t *mac_addr, const uint8_t *data, int len) - sesuaikan.
// ------------------------------------------------------------------
static void espnow_recv_callback(const esp_now_recv_info_t *recv_info,
                                  const uint8_t *data, int len)
{
    if (len != sizeof(aether_packet_t)) {
        ESP_LOGW(TAG, "Terima paket ESP-NOW ukuran tidak dikenal: %d byte", len);
        return;
    }
    aether_packet_t pkt;
    memcpy(&pkt, data, sizeof(pkt));

    // Sengaja pakai ESP_LOGI (bukan LOGD) supaya kelihatan di monitor default -
    // ini bukti langsung ESP-NOW-nya benar-benar nyampe, dipakai buat debug.
    ESP_LOGI(TAG, "ESP-NOW diterima dari node %d, seq=%lu, next_token=%d",
             pkt.sender_id, (unsigned long)pkt.seq_num, pkt.next_token);

    if (pkt.next_token == NODE_ID) {
        s_my_turn_pending = true;
    }
}

// ------------------------------------------------------------------
// Kirim satu "slot" giliran: beberapa paket beruntun, lalu oper token.
// ------------------------------------------------------------------
static void send_my_slot(const uint8_t broadcast_mac[6])
{
    uint8_t next_id = (NODE_ID + 1) % TOTAL_NODES;

    for (int i = 0; i < PACKETS_PER_SLOT; i++) {
        aether_packet_t pkt = {
            .sender_id = NODE_ID,
            .seq_num = s_seq_counter++,
            // token cuma dititipkan di paket TERAKHIR slot ini
            .next_token = (i == PACKETS_PER_SLOT - 1) ? next_id : NODE_ID,
        };
        esp_err_t err = esp_now_send(broadcast_mac, (uint8_t *)&pkt, sizeof(pkt));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_now_send gagal: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(SLOT_DURATION_MS / PACKETS_PER_SLOT));
    }
}

// ------------------------------------------------------------------
// Inisialisasi WiFi (mode STA, tanpa konek ke AP - cuma butuh radio aktif
// untuk ESP-NOW + CSI) dan ESP-NOW.
// ------------------------------------------------------------------
static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(AETHER_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE));

    // Aktifkan mode promiscuous/sniffer - menurut dokumentasi Espressif,
    // station yang tidak terhubung ke AP kadang tidak memproses semua
    // paket yang diterima kecuali sniffer mode diaktifkan. Kita tidak
    // pasang callback promiscuous (tidak perlu data mentahnya di situ),
    // ini cuma supaya jalur penerimaan paket dibuka lebih lebar untuk CSI.
    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA,
    };
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&filter));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(promiscuous_rx_callback));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));

    // --- Aktifkan CSI ---
    // NOTE: field wifi_csi_config_t bisa beda antar versi IDF, cek header lokal.
    wifi_csi_config_t csi_config = {
        .lltf_en = true,
        .htltf_en = true,
        .stbc_htltf2_en = true,
        .ltf_merge_en = true,
        .channel_filter_en = false,
        .manu_scale = false,
        .shift = 0,
    };
    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(csi_rx_callback, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));

    // --- Init ESP-NOW ---
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_callback));

    // Tambahkan semua node lain sebagai peer ESP-NOW (unicast),
    // supaya bisa saling kirim & posisi MAC dikenali.
    for (int i = 0; i < TOTAL_NODES; i++) {
        if (i == NODE_ID) continue;
        esp_now_peer_info_t peer = {0};
        memcpy(peer.peer_addr, NODE_MAC_TABLE[i], 6);
        peer.channel = AETHER_WIFI_CHANNEL;
        peer.ifidx = WIFI_IF_STA;
        peer.encrypt = false;
        esp_err_t err = esp_now_add_peer(&peer);
        if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
            ESP_LOGW(TAG, "Gagal tambah peer node %d: %s", i, esp_err_to_name(err));
        }
    }

    // PENTING: alamat broadcast (FF:FF:FF:FF:FF:FF) juga harus didaftarkan
    // sebagai peer secara eksplisit - esp_now_send() ke broadcast akan gagal
    // dengan ESP_ERR_ESPNOW_NOT_FOUND kalau ini dilewatkan.
    {
        esp_now_peer_info_t bcast_peer = {0};
        memset(bcast_peer.peer_addr, 0xFF, 6);
        bcast_peer.channel = AETHER_WIFI_CHANNEL;
        bcast_peer.ifidx = WIFI_IF_STA;
        bcast_peer.encrypt = false;
        esp_err_t err = esp_now_add_peer(&bcast_peer);
        if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
            ESP_LOGW(TAG, "Gagal tambah broadcast peer: %s", esp_err_to_name(err));
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init();

    ESP_LOGI(TAG, "AETHER Node %d siap. Total node: %d", NODE_ID, TOTAL_NODES);

    // Node 0 yang memulai siklus pertama kali saat boot.
    if (NODE_ID == 0) {
        s_my_turn_pending = true;
    }

    // Broadcast MAC dipakai supaya semua node bisa dengar (untuk CSI callback),
    // walau payload token cuma relevan untuk 1 node berikutnya.
    uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    while (1) {
        if (s_my_turn_pending) {
            s_my_turn_pending = false;
            vTaskDelay(pdMS_TO_TICKS(GUARD_INTERVAL_MS));
            send_my_slot(broadcast_mac);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}