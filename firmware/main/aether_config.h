/*
 * aether_config.h
 * ----------------
 * Konfigurasi bersama untuk semua node AETHER (RTI network).
 * Tiap node di-flash dengan file yang SAMA, tinggal ganti NODE_ID
 * saat build (lihat catatan di bawah).
 */

#pragma once

#include <stdint.h>
#include "sdkconfig.h"

// NODE_ID dan TOTAL_NODES sekarang diatur lewat Kconfig (lihat
// main/Kconfig.projbuild), bukan lewat -DNODE_ID di command line lagi -
// cara lama itu terbukti tidak reliable (lihat catatan di README).
// Nilainya di-set lewat file sdkconfig.node0 / sdkconfig.node1 di root
// folder firmware/, masing-masing dipakai untuk build folder terpisah.
#define NODE_ID CONFIG_AETHER_NODE_ID
#define TOTAL_NODES CONFIG_AETHER_TOTAL_NODES

// ============================================================
// MAC address tiap node, didapat dari:
//   esptool --port COMx chip_id  (lihat baris "MAC: ...")
// Urutan index = NODE_ID.
// ============================================================
static const uint8_t NODE_MAC_TABLE[TOTAL_NODES][6] = {
    {0x8C, 0xBF, 0xEA, 0x16, 0x03, 0xDC},  // Node 0 - COM7, sudah dicek 23 Sep 2026
    {0xB8, 0x1F, 0x3F, 0xBB, 0xCA, 0x94},  // Node 1 - COM6, sudah dicek 23 Sep 2026
};

// WiFi channel yang dipakai bersama semua node (harus sama semua)
#define AETHER_WIFI_CHANNEL 6

// Timing round-robin (dalam ms)
#define SLOT_DURATION_MS        100   // durasi 1 giliran kirim
#define GUARD_INTERVAL_MS       20    // jeda antar giliran (hindari tabrakan)
#define PACKETS_PER_SLOT        5     // berapa paket dikirim tiap giliran node

// Tag untuk logging serial (dipakai csi_logger_v2.py buat parsing)
#define LOG_TAG_CSI "AETHER_CSI"