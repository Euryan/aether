# AETHER Node Firmware — Skeleton

Firmware generic untuk node RTI (ESP32-S3). Semua node pakai source code yang
sama; yang beda cuma `NODE_ID` yang di-set saat build.

## 1. Langkah pertama: cari MAC address tiap ESP32-S3

Sebelum edit `aether_config.h`, kamu perlu tahu MAC address fisik tiap board.
Cara cepat: flash contoh `get_started/hello_world` bawaan ESP-IDF lalu tambahkan
baris ini di `app_main()`:

```c
uint8_t mac[6];
esp_wifi_get_mac(WIFI_IF_STA, mac);  // panggil setelah esp_wifi_init()
printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
```

Atau lebih simpel: `esptool.py --port COMx chip_id` biasanya juga menampilkan
MAC address. Catat MAC tiap board (kasih label fisik di board-nya juga,
misal stiker "Node 0", "Node 1"), lalu isi ke `NODE_MAC_TABLE` di
`main/aether_config.h`.

## 2. Build & flash

```bash
cd firmware
idf.py set-target esp32s3

# Flash Node 0:
idf.py -DNODE_ID=0 -p COMx build flash monitor

# Flash Node 1 (board berbeda, port berbeda):
idf.py -DNODE_ID=1 -p COMy build flash monitor
```

Ulangi untuk Node 2 dan Node 3 nanti setelah board-nya sampai. Untuk testing
awal dengan 2 node, ubah dulu `TOTAL_NODES` di `aether_config.h` jadi `2`.

## 3. Yang perlu dicek/sesuaikan (karena field API bisa beda per versi IDF)

- `wifi_csi_config_t` di `wifi_init()` — buka
  `$IDF_PATH/components/esp_wifi/include/esp_wifi_types.h` di instalasi IDF
  kamu, cocokkan nama field.
- Signature `esp_now_register_recv_cb` — versi IDF v5.x pakai
  `esp_now_recv_info_t*`, versi v4.x pakai `const uint8_t *mac_addr` langsung.
  Cek `esp_now.h` kalau ada error compile di titik ini.
- Jalankan `idf.py --version` untuk tahu versi IDF yang terpasang.

## 4. Output serial

Tiap node akan print baris seperti ini tiap CSI diterima dari node lain:

```
AETHER_CSI,link=0-1,rssi=-63,rate=11,len=128,csi=[0 0 -3 -19 ...]
```

`link=0-1` artinya sinyal dari Node 0, diterima oleh Node 1 (node yang
sedang menjalankan firmware ini). Baris ini yang akan diparse oleh
`backend/csi_logger_v2.py`.
