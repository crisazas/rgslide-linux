# rgslide-linux: Native Linux on the Anbernic RG Slide (Unisoc T820 / ums9620_2h10)

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

- `rootfs/overlay/`: Complete Debian system overlay (systemd units, network configuration, sound daemons, hotkey scripts).
- `tools/`: Source code for custom utilities:
  - `rgslide-ui`: On-screen SDL2 quick settings and tools menu with multi-language support.
  - `tlspad`: `LD_PRELOAD` TLS padding library preventing Mali driver TLS collisions in glibc binaries.
  - `ahbdrm` / `drmshow`: Direct DRM/KMS testing tools and frame allocators.
  - `spd_dump`: Unisoc BROM protocol flashing and partition dumping utility.
  - `mkbootimg` / `unpack_bootimg` / `avbtool.py`: Android boot image manipulation tools.
- `patches/`: Custom patches applied during build:
  - `libhybris/`: 64-bit arm64 Bionic unwinder, STB_GNU_UNIQUE support, and GLVND integration.
  - `sdl2/`: Custom SDL2-hybris build with ALSA audio enabled.
  - `retroarch/`: `-DDT_NO_POOL` TLS fixes and RetroArch customizations.
  - `es/`: ES-DE KMS/EGL patches and sound triggers.
- `scripts/`: Modular build pipeline:
  - `build-rootfs.sh`: Bootstraps Debian 13 (Trixie) rootfs using `mmdebstrap` / `debootstrap`.
  - `build-libhybris.sh` / `build-sdl2-hybris.sh`: Compiles hybris graphics stack and SDL2.
  - `build-esde.sh` / `build-retroarch.sh`: Builds ES-DE frontend and RetroArch cores.
  - `build-full-rootfs.sh`: Assembles the final bootable `rootfs-full.ext4` image with all overlays, modules, and blobs.
  - `pack-boot.sh` / `build-initramfs.sh`: Packages `boot.img` and `initramfs`.
- `docs/`: In-depth reverse engineering notes, partition layouts, audio codec registers, and bring-up documentation.

---

## 🏗️ Building the Image

### Prerequisites (Host PC)
- Linux x86_64 or aarch64 (Ubuntu 22.04+ or Debian 12+ recommended).
- Installed packages: `debootstrap`, `mmdebstrap`, `qemu-user-static`, `binfmt-support`, `e2fsprogs`, `cmake`, `ninja-build`, `gcc-aarch64-linux-gnu`, `g++-aarch64-linux-gnu`.
- Stock partition dump from the console (`super`, `boot`, `vendor_boot`, and `userdata`), placed in `dump/`.

### Build Pipeline
1. **Extract Vendor Blobs and Kernel Modules:**
   ```bash
   ./scripts/extract-android-bits.sh
   ```
2. **Build Rootfs Base & Native Binaries:**
   ```bash
   ./scripts/build-rootfs.sh --no-image
   ./scripts/build-libhybris.sh
   ./scripts/build-sdl2-hybris.sh
   ./scripts/build-esde.sh
   ./scripts/build-retroarch.sh
   ```
3. **Assemble Final Rootfs Image:**
   ```bash
   ./scripts/build-full-rootfs.sh out/rootfs-full.ext4
   ```
4. **Flash to Console (via BROM / Fastboot):**
   ```bash
   # Enter fastboot and flash to testing slot (Slot A)
   fastboot flash userdata out/rootfs-full.ext4
   ```

---

## ⚠️ Disclaimer

This project replaces the stock Android operating system. Proprietary firmware and Android vendor blobs (`dump/`) are not distributed in this repository due to copyright restrictions; you must dump them from your own device before assembling the final rootfs.
