"""
CSI Logger untuk ESP32-CSI-Tool
================================
Membaca output serial dari ESP32 (format ESP32-CSI-Tool), parse baris CSI_DATA,
dan simpan ke CSV terstruktur untuk analisis lebih lanjut.

Cara pakai:
python csi_logger.py --port COM6 --output data_kosong.csv --label kosong --duration 30
Tekan Ctrl+C untuk berhenti logging.
"""

import serial
import csv
import argparse
import time
import re
import sys


def parse_csi_line(line: str):
    """
    Parse satu baris CSI_DATA dari ESP32-CSI-Tool.
    Format: CSI_DATA,role,mac,rssi,rate,sig_mode,mcs,bandwidth,smoothing,
             not_sounding,aggregation,stbc,fec_coding,sgi,noise_floor,
             ampdu_cnt,channel,secondary_channel,local_timestamp,ant,
             sig_len,rx_state,real_time_set,real_timestamp,len,CSI_DATA
    Mengembalikan dict, atau None kalau baris tidak valid / bukan baris CSI_DATA.
    """
    if not line.startswith("CSI_DATA"):
        return None

    # Pisahkan bagian sebelum '[' (metadata) dan di dalam '[...]' (data CSI)
    match = re.match(r"^(.*?)\[(.*)\]\s*$", line.strip())
    if not match:
        return None

    metadata_str, csi_str = match.groups()
    fields = metadata_str.strip().rstrip(",").split(",")

    if len(fields) < 25:
        return None

    try:
        record = {
            "type": fields[0],
            "role": fields[1],
            "mac": fields[2],
            "rssi": int(fields[3]),
            "rate": int(fields[4]),
            "sig_mode": int(fields[5]),
            "mcs": int(fields[6]),
            "bandwidth": int(fields[7]),
            "channel": int(fields[16]),
            "local_timestamp": fields[18],
            "len": int(fields[24]),
        }
    except (ValueError, IndexError):
        return None

    # Parse array CSI mentah jadi list of int
    try:
        csi_raw = [int(x) for x in csi_str.strip().split()]
    except ValueError:
        return None

    record["csi_raw"] = csi_raw
    return record


def main():
    parser = argparse.ArgumentParser(description="Log data CSI dari ESP32 ke CSV")
    parser.add_argument("--port", required=True, help="COM port, misal COM6")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate")
    parser.add_argument("--output", required=True, help="Nama file CSV output")
    parser.add_argument("--label", default="unknown",
                         help="Label kondisi eksperimen, misal 'kosong' atau 'ada_orang'")
    parser.add_argument("--duration", type=int, default=0,
                         help="Durasi logging dalam detik (0 = tanpa batas, stop manual pakai Ctrl+C)")
    args = parser.parse_args()

    print(f"Membuka {args.port} @ {args.baud} baud...")
    ser = serial.Serial(args.port, args.baud, timeout=1)
    time.sleep(2)  # beri waktu ESP32 reset setelah port dibuka

    fieldnames = ["timestamp_pc", "label", "mac", "rssi", "rate", "sig_mode",
                  "mcs", "bandwidth", "channel", "len", "csi_raw"]

    count = 0
    start_time = time.time()

    with open(args.output, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()

        print(f"Logging dimulai. Label kondisi: '{args.label}'. Tekan Ctrl+C untuk stop.")
        try:
            while True:
                if args.duration > 0 and (time.time() - start_time) > args.duration:
                    print(f"\nDurasi {args.duration}s tercapai, logging selesai.")
                    break

                try:
                    raw_line = ser.readline().decode("utf-8", errors="ignore")
                except Exception:
                    continue

                if not raw_line:
                    continue

                record = parse_csi_line(raw_line)
                if record is None:
                    continue

                writer.writerow({
                    "timestamp_pc": time.time(),
                    "label": args.label,
                    "mac": record["mac"],
                    "rssi": record["rssi"],
                    "rate": record["rate"],
                    "sig_mode": record["sig_mode"],
                    "mcs": record["mcs"],
                    "bandwidth": record["bandwidth"],
                    "channel": record["channel"],
                    "len": record["len"],
                    "csi_raw": " ".join(map(str, record["csi_raw"])),
                })
                count += 1
                if count % 20 == 0:
                    print(f"  {count} paket CSI tersimpan...")

        except KeyboardInterrupt:
            print(f"\nDihentikan manual. Total {count} paket tersimpan ke {args.output}")
        finally:
            ser.close()

    print(f"Selesai. File tersimpan: {args.output}")


if __name__ == "__main__":
    main()
