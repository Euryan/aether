import pandas as pd
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

def load_and_filter(path, label, mac_filter=None):
    df = pd.read_csv(path)
    if mac_filter:
        df = df[df["mac"] == mac_filter]
    # hanya ambil baris dengan len konsisten (256, mode HT paling banyak muncul)
    most_common_len = df["len"].mode()[0]
    df = df[df["len"] == most_common_len].reset_index(drop=True)
    print(f"[{label}] total baris dipakai: {len(df)} (len={most_common_len} byte, mac={df['mac'].unique()})")
    return df

def csi_to_amplitude_matrix(df):
    """Convert csi_raw string -> matrix (n_packets, n_subcarriers) amplitude"""
    amp_list = []
    for raw in df["csi_raw"]:
        vals = np.array([int(x) for x in raw.split()])
        imag = vals[0::2]
        real = vals[1::2]
        amp = np.sqrt(imag.astype(float)**2 + real.astype(float)**2)
        amp_list.append(amp)
    return np.vstack(amp_list)

kosong = load_and_filter("./data_kosong.csv", "kosong")
ada_orang = load_and_filter("./data_ada_orang.csv", "ada_orang")

amp_kosong = csi_to_amplitude_matrix(kosong)
amp_ada = csi_to_amplitude_matrix(ada_orang)

n_sub = min(amp_kosong.shape[1], amp_ada.shape[1])
amp_kosong = amp_kosong[:, :n_sub]
amp_ada = amp_ada[:, :n_sub]

mean_kosong = amp_kosong.mean(axis=0)
mean_ada = amp_ada.mean(axis=0)
var_kosong = amp_kosong.var(axis=0)
var_ada = amp_ada.var(axis=0)

print("\n=== RINGKASAN ===")
print(f"Rata-rata RSSI kosong    : {kosong['rssi'].mean():.1f} dBm (std {kosong['rssi'].std():.2f})")
print(f"Rata-rata RSSI ada_orang : {ada_orang['rssi'].mean():.1f} dBm (std {ada_orang['rssi'].std():.2f})")
print(f"Rata-rata variance amplitude (semua subcarrier) - kosong    : {var_kosong.mean():.3f}")
print(f"Rata-rata variance amplitude (semua subcarrier) - ada_orang : {var_ada.mean():.3f}")
print(f"Rasio variance ada_orang / kosong: {var_ada.mean() / var_kosong.mean():.2f}x")

# Plot 1: Mean amplitude per subcarrier
fig, axes = plt.subplots(2, 1, figsize=(12, 9))

axes[0].plot(mean_kosong, label="Kosong", color="#2563eb", linewidth=1.5)
axes[0].plot(mean_ada, label="Ada Orang", color="#dc2626", linewidth=1.5)
axes[0].set_title("Rata-rata Amplitude CSI per Subcarrier")
axes[0].set_xlabel("Index Subcarrier")
axes[0].set_ylabel("Amplitude")
axes[0].legend()
axes[0].grid(alpha=0.3)

axes[1].plot(var_kosong, label="Kosong", color="#2563eb", linewidth=1.5)
axes[1].plot(var_ada, label="Ada Orang", color="#dc2626", linewidth=1.5)
axes[1].set_title("Variance Amplitude CSI per Subcarrier (indikator gerakan/objek)")
axes[1].set_xlabel("Index Subcarrier")
axes[1].set_ylabel("Variance")
axes[1].legend()
axes[1].grid(alpha=0.3)

plt.tight_layout()
plt.savefig("./csi_analysis.png", dpi=150)
print("\nGrafik disimpan: ./csi_analysis.png")

# Plot 2: Time series total variance (rolling) untuk lihat kapan ada gerakan
fig2, ax2 = plt.subplots(figsize=(12, 4))
window_var_kosong = pd.Series(amp_kosong.mean(axis=1)).rolling(10).std()
window_var_ada = pd.Series(amp_ada.mean(axis=1)).rolling(10).std()
ax2.plot(window_var_kosong.values, label="Kosong", color="#2563eb")
ax2.plot(window_var_ada.values, label="Ada Orang", color="#dc2626")
ax2.set_title("Rolling Std-Dev Amplitude Rata-rata Sepanjang Waktu (window=10 paket)")
ax2.set_xlabel("Index Paket")
ax2.set_ylabel("Rolling Std-Dev")
ax2.legend()
ax2.grid(alpha=0.3)
plt.tight_layout()
plt.savefig("./csi_timeline.png", dpi=150)
print("Grafik disimpan: ./csi_timeline.png")