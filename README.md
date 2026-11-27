# rgslide-linux: Native Linux on the Anbernic RG Slide (Unisoc T820 / ums9620_2h10)

> **Language:** English | [Español (README.es.md)](README.es.md)
> *(Internal reverse-engineering notes in `docs/` and commit history are maintained in Spanish.)*

> **Status: Experimental Pre-Release (`v0.9.x`) — NOT production-ready `1.0`.**
> Boot stability still has open validation items (a stock-module race condition between `apsys_dvfs` and `mmdvfs` caused 2/14 measured boot panics; a `softdep` workaround is deployed with limited cold-boot verification so far), deep sleep (`s2idle`) is restricted, and USB-C DisplayPort output does not work yet.

Native Linux distribution (Debian 13 Trixie arm64) for the **Anbernic RG Slide** handheld gaming console, replacing Android with hardware-accelerated 3D graphics (Mali-G57 GPU via `libhybris`), EmulationStation Desktop Edition (ES-DE) frontend, RetroArch, audio, Wi-Fi, Bluetooth, and full physical controls.

---

## Project Status: What Works & Known Limitations

### Working

* **Display & 3D Hardware Acceleration (Mali-G57 GPU):**
  - Native display panel running at full resolution (1280x960) with proper orientation and up to 120 Hz refresh rate.
  - **Vulkan 1.3 (Native Linux):** Open-source **Mesa PanVK 26.3** (`libvulkan_panfrost.so`) talking directly to `/dev/mali0` (`mali_kbase` JM v9) with zero-copy `VK_KHR_display` presentation on `/dev/dri/card0` (60/90/120 Hz) — no `libhybris`, no `tlspad`, no Android blobs.
  - **OpenGL ES 2.0 / 3.x:** Hardware acceleration through `libhybris` + Android vendor Mali blob + `libtlspad.so`.
  - **EmulationStation Desktop Edition (ES-DE 3.5.0)** and **RetroArch (1.22.2)** running natively (RetroArch supports both Vulkan + `.slang` shaders via `khr_display` and OpenGL ES).
* **Audio Subsystem:**
  - Stereo audio output via built-in speakers and 3.5 mm headphone jack through the Unisoc UMP9620 codec (`snd_soc_sprd_codec_ump9620`).
  - Regulator softdep ordering (`rgslide-audio.conf`) mitigating boot static noise / `DPOP`.
  - Dedicated ALSA audio mixing daemon (`rgslide-audiod`) for UI navigation sounds and in-game audio.
* **Controls, Slider & Standby:**
  - Integrated internal gamepad (`retrogame_joypad`, `/dev/input/event3`) mapped and calibrated.
  - Physical volume rocker (`/dev/input/event0` via `gpio-keys`) integrated with `rgslide-hotkeyd` and `rgslide-volume`.
  - Hall slider sensor (`KEY_WAKEUP`) and short Power key press trigger instant low-power standby (`rgslide-sleepd`): blanks backlight, pauses (`SIGSTOP`) ES-DE/RetroArch mid-game, and drops CPUs to `powersave`; sliding open resumes immediately.
  - On-screen quick menu / tools overlay (`rgslide-ui`) accessible via `Back + Select`.
* **Connectivity & Networking:**
  - **Wi-Fi:** 2.4 / 5 GHz station mode with DHCP via `systemd-networkd`.
  - **Bluetooth:** Hardware initialization (`rgslide-bt-init`), `bluetoothd`, and pairing helper (`rgslide-bt-pair`).
  - **USB:** USB Gadget network (`rndis` / `cdc_ether`) for development and SSH access (`console@10.55.0.2`).
* **System & Integration:**
  - Minimal Debian 13 (Trixie) arm64 base.
  - CM4 coprocessor kept alive (`rgslide-cm4-prep`) to prevent the ~284 s hardware watchdog reboot.
  - Hardware RTC (`sc27xx-rtc`) persistence + automatic SNTP synchronization (`rgslide-clock`) with `/var/lib/rgslide/lasttime` fallback.
  - Dynamic language synchronization (English / Spanish) between ES-DE and the Tools menu.

### Known Limitations & Open Issues

* **Boot Race Condition (`apsys_dvfs` vs `mmdvfs`):**
  - Stock modules `apsys-dvfs.ko` and `mmdvfs.ko` both register the `vdsp_dvfs` devfreq governor, causing a kernel use-after-free panic on retry when `mmdvfs` wins the race (measured at 2 of 14 boots before mitigation; see [`docs/agent-panic.md`](docs/agent-panic.md)). A `softdep mmdvfs pre: apsys_dvfs` rule is deployed, but still requires extended cold-boot soak testing.
* **Kernel Deep Sleep (`mem` / `s2idle`):**
  - Currently handled via instant userspace standby (`rgslide-sleepd`) because stock kernel 5.4 `mem`/`s2idle` hangs in Unisoc PM notifiers (`sprd_pcm_pm_notifier`) when audio/DRM are active.
* **External Video Output (USB-C DisplayPort / HDMI Alt Mode):**
  - Not working; DP negotiation and display muxing remain unimplemented.
* **Single-Client ALSA Mixing:**
  - Audio streams currently route through the `rgslide-audiod` pipeline rather than direct multi-client ALSA mixing.

---

## Technical Debt & Architectural Trade-offs

Before flashing or contributing, be aware of the structural workarounds currently holding the graphics stack together:

1. **Fragile TLS ABI Padding (`libtlspad.so`):**
   - `tools/tlspad/` reserves a 64-byte-aligned `__thread char[512]` block at the start of static TLS so glibc variables do not collide with Bionic TLS slots hardcoded by `libGLES_mali.so` (`[tp+0x00..0x70]`). This depends strictly on `LD_PRELOAD` ordering; as noted in [`docs/quirks.md`](docs/quirks.md), the TLS heuristic is **not reliable** and can break if preload order or linked libraries change.
2. **Four Live Android Services Under `linker64`:**
   - `servicemanager`, `vndservicemanager`, `hwservicemanager`, and `android.hardware.graphics.allocator@4.0-service` must stay running under Android's `linker64` (uid `1000`) so the Mali blob can allocate `gralloc` buffers.
3. **Single DRM Master Limit:**
   - Only one process can hold DRM master at a time. ES-DE is compiled with `DEINIT_ON_LAUNCH=ON` to release DRM before launching RetroArch or `rgslide-ui`.
4. **Stock 5.4.233 Kernel (No Mainline / No Panfrost):**
   - The system boots the stock Anbernic/Unisoc Android Linux 5.4.233 kernel binary with vendor modules. There is no mainline kernel or open-source Panfrost GPU driver support yet.

---

## Default Credentials & Network Surface

Prebuilt images ship with development-friendly defaults enabled out of the box:

- **Default user:** `console` / password `console` (with `sudo`)
- **Root account:** password `root` (`PermitRootLogin prohibit-password` in `sshd_config`)
- **Always-on network services:** `sshd` and USB Gadget RNDIS (`10.55.0.2`) are enabled at boot.

**Recommendation:** Change both `console` and `root` passwords immediately after first boot (`passwd` and `sudo passwd root`), and consider adding SSH keys and disabling password authentication if connecting the handheld to untrusted Wi-Fi networks.

---

## Flashing Guide

### **STEP 0 — MANDATORY BACKUP BEFORE FLASHING (DO NOT SKIP)**

**Flashing `rgslide-linux` overwrites `userdata` and replaces Android/GammaOS completely. There is NO dual-boot fallback.**

**Before running the flasher on a stock console, you MUST dump your stock partitions via BROM** (using `scripts/dump-min.sh` or `tools/spd_dump`), or ensure you have a copy of the official Anbernic RG Slide stock firmware from [Anbernic's official downloads page](https://win.anbernic.com/download/) to restore via BROM/ResearchDownload. **Without a Phase-0 BROM dump or stock firmware package, you cannot roll back to Android/GammaOS.**

### Unified A/B Slot Convention

This project uses a single, consistent slot convention across releases, `tools/flash-rgslide.sh`, `README.md`, and `AGENTS.md`:

- **Slot B (`boot_b`):** **Known-good / release installation.** Prebuilt BROM release packages write `boot_b`, `misc` (selecting Slot B), and `userdata`.
- **Slot A (`boot_a`):** **Testing / development iteration.** Contributors iterating via `fastboot` flash experimental boot images to Slot A so Slot B remains intact as a known-good fallback.

### Flashing a Prebuilt Release (BROM Mode -> Slot B)

The flashing script is versioned and auditable in [`tools/flash-rgslide.sh`](tools/flash-rgslide.sh).

1. Extract the release `.tar.zst` archive on a Linux x86_64 host and verify `SHA256SUMS`.
2. Preview the exact flash command safely without touching the device:
   ```bash
   ./flash-rgslide.sh --dry-run
   ```
3. Power off the RG Slide completely (hold `Vol-Down + Power` if frozen).
4. Hold the **Back** button on the right side of the screen and connect USB-C to your PC.
5. Run the flasher (writes `boot_b`, `misc`, and `userdata`, and prompts for `FLASH` confirmation):
   ```bash
   sudo ./flash-rgslide.sh
   ```

### Iterating During Development (Fastboot -> Slot A)

When testing a newly built `boot.img` or `userdata.ext4` without touching the known-good Slot B:

```bash
./tools/flash-rgslide.sh --mode fastboot --slot a --dir out --dry-run
./tools/flash-rgslide.sh --mode fastboot --slot a --dir out
```

`tools/flash-rgslide.sh` checks `fastboot getvar product` and `fastboot getvar current-slot` before writing.

---

## Architecture Overview

```
+-------------------------------------------------------------+
|         EmulationStation Desktop Edition (ES-DE 3.5.0)      |
|                       RetroArch (1.22.2)                    |
+-------------------------------------------------------------+
|     SDL2-hybris (ALSA audio)        |     EGL / GLES v2/3   |
+-------------------------------------+-----------------------+
|  libhybris (linker & hooks) + libtlspad (64B TLS alignment) |
+-------------------------------------------------------------+
|  Android Vendor Blobs (libGLES_mali.so + 4 binder services) |
+-------------------------------------------------------------+
|    Linux Kernel 5.4.233 (Stock Anbernic / Unisoc T820)      |
+-------------------------------------------------------------+
|             Anbernic RG Slide Hardware (ums9620)            |
+-------------------------------------------------------------+
```

---

## Repository Layout

- `rootfs/overlay/`: Debian system overlay (systemd units, modprobe rules, network config, audio daemons, hotkey scripts).
- `tools/`: Custom utilities and vendored Android/BROM tools (pinned in [`MANIFEST.md`](MANIFEST.md)):
  - `flash-rgslide.sh`: Safe BROM/fastboot flashing script with `--dry-run`, slot checks, and confirmation.
  - `rgslide-ui`: On-screen SDL2 quick settings and tools menu (EN/ES).
  - `tlspad`: `LD_PRELOAD` TLS padding shim.
  - `ahbdrm` / `drmshow`: Direct DRM/KMS test tools and frame allocators.
  - `spd_dump`, `mkbootimg.py`, `unpack_bootimg.py`, `avbtool.py`, `lpunpack`: Flashing and Android image tools.
- `patches/`: Patches applied to `libhybris`, `sdl2`, `retroarch`, and `es`.
- `scripts/`: Modular build pipeline (`build-rootfs.sh`, `build-libhybris.sh`, `build-sdl2-hybris.sh`, `build-esde.sh`, `build-retroarch.sh`, `build-full-rootfs.sh`, `pack-boot.sh`, `build-initramfs.sh`).
- `docs/`: Reverse-engineering notes, partition layouts, audio codec registers, panic analyses, and bring-up quirks (in Spanish).

---

## Building from Source

### Prerequisites (Host PC)
- Linux x86_64 or aarch64 (Ubuntu 22.04+ or Debian 12+ recommended).
- Packages: `debootstrap`, `mmdebstrap`, `qemu-user-static`, `binfmt-support`, `e2fsprogs`, `cmake`, `ninja-build`, `gcc-aarch64-linux-gnu`, `g++-aarch64-linux-gnu`, `device-tree-compiler`, `android-sdk-libsparse-utils`.
- Stock partition dump from your own RG Slide (`super`, `boot`, `vendor_boot`), placed in `dump/`.

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
4. **Flash to Testing Slot (Slot A) via Fastboot:**
   ```bash
   ./tools/flash-rgslide.sh --mode fastboot --slot a --dir out
   ```

---

## Licensing, GPL Source Offer & Proprietary Blobs Disclosure

- **Original code:** Licensed under the **MIT License** — see [`LICENSE`](LICENSE).
- **Third-party attributions & GPLv3 source offer:** See [`NOTICE`](NOTICE) and [`THIRD-PARTY.md`](THIRD-PARTY.md), which include exact upstream commits, local patches, and source tarball links for **RetroArch 1.22.2** (GPLv3), bundled **libretro cores**, **ES-DE 3.5.0**, **libhybris**, **SDL2**, and vendored tools.
- **Pinned tool checksums:** See [`MANIFEST.md`](MANIFEST.md).
- **Proprietary Blobs Disclosure:**
  - This **Git repository** does **not** store proprietary firmware or vendor blobs (`dump/` and `out/` are gitignored).
  - **Prebuilt release archives** (`rgslide-linux-brom-*.tar.zst`) **DO redistribute** proprietary binaries extracted from stock Anbernic RG Slide firmware inside `userdata.ext4` (`/opt/android` including Arm `libGLES_mali.so` ~43 MB, Android vendor/bionic libraries, stock 5.4.233 kernel modules, Unisoc Wi-Fi/BT firmware) as well as `boot.img` and `fdl1`/`fdl2` loaders. These files are redistributed unofficially for personal use and interoperability on RG Slide hardware owned by the user, without an explicit license from Arm, Unisoc, or Anbernic. All rights remain with their respective copyright holders.
