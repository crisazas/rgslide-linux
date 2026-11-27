# rgslide: Native Linux on the Anbernic RG Slide (Unisoc T820 / ums9620_2h10)

Native Linux distribution (Debian 13 Trixie arm64) for the **Anbernic RG Slide** handheld gaming console, completely replacing Android with hardware-accelerated 3D graphics (Mali-G57 GPU), EmulationStation Desktop Edition (ES-DE) frontend, audio, Wi-Fi, Bluetooth, and full physical controls.

---

## 🎮 Project Status: What Works & Known Limitations

### ✅ Fully Working

* **Display & 3D Hardware Acceleration (Mali-G57 GPU):**
  - Native display panel running at full resolution with proper orientation and high refresh rate (up to 120 Hz).
  - OpenGL ES 2.0 / 3.x hardware acceleration through `libhybris` + vendor Mali blob + custom TLS padding (`tlspad`).
  - **EmulationStation Desktop Edition (ES-DE)** and **RetroArch** running natively with OpenGL acceleration.
* **Audio Subsystem:**
  - Stereo audio output via built-in speakers and 3.5 mm headphone jack through the Unisoc UMP9620 codec (`snd_soc_sprd_codec_ump9620`).
  - Resolved regulator loading race condition (`SRG_VB`, `SRG_BIAS`) eliminating boot static noise and pop sounds (`DPOP`).
  - Dedicated ALSA audio mixing daemon (`rgslide-audiod`) providing low-latency UI navigation sounds and in-game audio.
* **Controls & Physical Buttons:**
  - Integrated internal gamepad (`retrogame_joypad`, `/dev/input/event3`) fully mapped and calibrated.
  - Physical volume rocker (`/dev/input/event0` via `gpio-keys`) integrated with `rgslide-hotkeyd` and `rgslide-volume`.
  - On-screen quick menu / tools overlay (`rgslide-ui`) accessible via `Back + Select`.
* **Connectivity & Networking:**
  - **Wi-Fi:** Reliable 2.4 / 5 GHz station mode with automatic DHCP configuration via `systemd-networkd`.
  - **Bluetooth:** Hardware initialization (`rgslide-bt-init`), `bluetoothd` daemon active, and device pairing utility (`rgslide-bt-pair`) for controllers and audio devices.
  - **USB:** USB Gadget network (`rndis` / `cdc_ether`) for development and SSH access (`console@10.55.0.2`).
* **System & Integration:**
  - Minimal Debian 13 (Trixie) arm64 base.
  - CM4 coprocessor kept alive and powered (`rgslide-cm4-prep`).
  - Dynamic language synchronization: The "Tools" settings menu automatically aligns with the active language in ES-DE (English / Spanish).

---

### ⏳ Work in Progress / Known Limitations

* **Deep Sleep (S2idle):**
  - Currently restricted to light sleep to prevent I2C/sensorhub bus lockups on resume.
* **External Video Output (USB-C DisplayPort / HDMI Alt Mode):**
  - DP negotiation and display muxing logic under development.
* **Front Camera:**
  - Not needed for retro emulation; Unisoc ISP userspace drivers are currently omitted.
* **Direct Multi-Client ALSA Mixing:**
  - Audio streams currently route through the `rgslide-audiod` pipeline.

---

## 🛠️ Architecture Overview

```
+-------------------------------------------------------------+
|              EmulationStation Desktop Edition               |
|                         RetroArch                           |
+-------------------------------------------------------------+
|     SDL2-hybris (with ALSA audio)   |     EGL / GLES v2     |
+-------------------------------------+-----------------------+
|  libhybris (linker & hooks) + tlspad (TLS 64-byte alignment)|
+-------------------------------------------------------------+
|          Android Vendor Blobs (libGLES_mali.so, etc.)       |
+-------------------------------------------------------------+
|    Linux Kernel 5.4.233 (Stock Anbernic / Unisoc T820)      |
+-------------------------------------------------------------+
|             Anbernic RG Slide Hardware (ums9620)            |
+-------------------------------------------------------------+
```

---

## 📂 Repository Layout

- `rootfs/overlay/`: Debian system overlay files (`/etc`, `/usr/local/bin`, `/usr/local/sbin`, systemd services).
- `tools/`: Native utilities and daemons (`rgslide-ui`, `rgslide-ctl`, `tlspad`, etc.).
- `patches/`: Source patches applied to third-party components (`libhybris`, kernel, SDL2).
- `scripts/`: Image generation, flashing, and partitioning scripts.
- `docs/`: Reverse-engineering notes, hardware bring-up logs, and technical references.

---

## ⚠️ Disclaimer

This project installs a native Linux system, replacing the stock Android installation. Reverting back to factory Android requires restoring the original partition dump via BROM mode (`spd_dump`).
