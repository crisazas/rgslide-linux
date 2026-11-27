# rgslide-linux: Native Linux on Anbernic Unisoc T820 Handhelds (RG Slide, RG406H, RG476H)

> **Language:** English | [Español (README.es.md)](README.es.md)
> *(Internal reverse-engineering notes in `docs/` and commit history are maintained in Spanish.)*

> **Status: Pre-Release (`v0.9.1`)**

Native Linux distribution (Debian 13 Trixie arm64) for **Anbernic Unisoc T820 (`ums9620_2h10`)** handheld gaming consoles (**RG Slide**, **RG406H**, and **RG476H**), replacing Android with hardware-accelerated 3D graphics (Mesa PanVK Vulkan 1.3 + Mali-G57 via `libhybris`), EmulationStation Desktop Edition (ES-DE) frontend, RetroArch, audio, Wi-Fi, Bluetooth, touchscreens, and physical controls.

---

## Supported Devices

| Device | Display | Refresh Rate | Form Factor & Controls | Profile Config |
| :--- | :--- | :--- | :--- | :--- |
| **Anbernic RG Slide** | 4.7" 1280x960 | Up to 120 Hz | Slider with Hall sensor (`KEY_WAKEUP`), `retrogame_joypad` | `devices/rgslide` |
| **Anbernic RG406H** | 4.0" 960x720 | 60 Hz | Horizontal, Hall sticks + triggers, RGB stick LEDs (`rg406h-leds`) | `devices/rg406h` |
| **Anbernic RG476H** | 4.7" 1280x960 | Up to 120 Hz | Horizontal, Hall sticks + triggers, AMUX multiplexer | `devices/rg476h` |

---

## Project Status: What Works & Known Limitations

### Working

* **Display & 3D Hardware Acceleration (Mali-G57 GPU):**
  - Native display panels running at full resolution (1280x960 or 960x720) with proper orientation and up to 120 Hz refresh rate.
  - **Vulkan 1.3 (Native Linux):** Open-source **Mesa PanVK 26.3** (`libvulkan_panfrost.so`) talking directly to `/dev/mali0` (`mali_kbase` JM v9) with zero-copy `VK_KHR_display` presentation on `/dev/dri/card0` (60/90/120 Hz) as well as Wayland/X11 WSI (`VK_KHR_wayland_surface`, `VK_KHR_xcb_surface`) across all supported devices — no `libhybris`, no `tlspad`, no Android blobs.
  - **OpenGL ES 2.0 / 3.x:** Hardware acceleration through `libhybris` + Android vendor Mali blob + `libtlspad.so`, with automatic `mali_kbase` cold-boot probe retry in `gpu-prep`.
  - **EmulationStation Desktop Edition (ES-DE 3.5.0)** and **RetroArch (1.22.2)** running natively (RetroArch defaults to Vulkan + `.slang` shaders via `khr_display`).
* **PortMaster & Native Ports Integration:**
  - **PortMaster** integrated directly into ES-DE's **Ports** system (`/opt/tools/PortMaster`, `/roms -> /home/console/ROMs`, automatic ES-DE rescan after installing or removing ports, and automatic download on fresh images).
  - **`rgslide-python3` (`tools/pytls/`):** Minimal CPython 3.13 launcher that reserves the first 512 bytes of main-executable static TLS and loads `libpython3.13.so.1.0` dynamically, resolving the `_Py_tss_tstate` vs Android Mali `TLS_SLOT_OPENGL` (`TPIDR_EL0 + 0x18`) collision cleanly.
  - **`SDL2-hybris` + `gptokeyb`/`gptokeyb2`:** Built with `libudev` (`HAVE_LIBUDEV_H=1`) for automatic hotplug detection of virtual `uinput` keyboards/mice and USB/Bluetooth peripherals, paired with dual-GUID `retrogame_joypad` mappings (`rgslide-gamecontrollerdb.txt`).
  - **SquashFS & Westonpack Runtimes:** Transparent `.squashfs` mounting via `squashfuse` (`/usr/sbin/mount.squashfs`), plus a native **Westonpack** replacement (`rgslide-westonwrap.sh`) running Debian Weston 14 (`drm` + `pixman`, `WESTON_DISABLE_ATOMIC=1`, `kiosk-shell`, `XWayland`, `scale=2`, `seatd` with `SEATD_VTBOUND=0`).
  - **Native AGS Engine (`rgslide-ags`):** Adventure Game Studio v3.6.3.17 compiled with native OpenGL ES 2.0 (`AGS_OPENGL_ES2=1`) running directly on the Mali-G57 GPU via `SDL2-hybris` (bypassing Weston) for AGS PortMaster games.
* **Audio Subsystem:**
  - Stereo audio output via built-in speakers and 3.5 mm headphone jack through the Unisoc UMP9620 codec (`snd_soc_sprd_codec_ump9620`).
  - Regulator softdep ordering (`rgslide-audio.conf`) mitigating boot static noise / `DPOP`.
  - True mute at volume level 0; dedicated ALSA audio daemon (`rgslide-audiod`) with 48 kHz FIFO backpressure pacing for UI navigation sounds, RetroArch, and SDL2/OpenAL/MojoAL/FMOD games.
* **Controls, Standby & Touch:**
  - Integrated gamepad (`retrogame_joypad`, `/dev/input/event3`) mapped and calibrated for each model with dedicated RetroArch autoconfig.
  - **Touchscreen (`fts_ts`):** Native multi-touch support integrated into RetroArch (`UDEV_TOUCH_SUPPORT`), filtering out the ghost `virtual_gamepad` node and mapping touches to pointer devices (`RETRO_DEVICE_POINTER`, NDS stylus via DeSmuME, lightgun, and XMB menu navigation).
  - Native in-game rumble / force feedback: RetroArch's `udev` joypad driver is patched to bridge `retrogame_joypad` to `sc27xx:vibrator` (16 FF effects) with zero extra daemons.
  - Physical volume rocker (`/dev/input/event0` via `gpio-keys`) integrated with `rgslide-hotkeyd` and `rgslide-volume`.
  - Instant low-power standby (`rgslide-sleepd`): blanks backlight, pauses (`SIGSTOP`) ES-DE/RetroArch mid-game, and drops CPUs to `powersave`. Triggered by short Power press or slider close (on RG Slide).
  - Single-press emergency app termination / rescue shortcut (`Select + Home`, 200 ms hold) via `hotkey-launch.py` and the Quick Menu (`rgslide-ui`), working across both RetroArch and PortMaster ports (cleanly terminating `gptokeyb`/`weston` and unmounting temporary `/tmp` `squashfuse` mounts).
  - Motion sensors: the CM4 sensorhub (`rgslide-ch`) feeds an on-demand DSU/Cemuhook UDP server (`rgslide-imud`, `127.0.0.1:26760`) with accelerometer data.
  - USB-C DisplayPort: `eglplatform_drm` prefers a connected DP/HDMI connector over the internal panel and `rgslide-dpd` handles hotplug. **Untested with a real monitor** (only the fallback to the internal panel has been verified).
  - On-screen quick menu / tools overlay (`rgslide-ui`) accessible via `Select + Home` (EN/ES).
* **Connectivity & Networking:**
  - **Wi-Fi:** 2.4 / 5 GHz station mode with DHCP via `systemd-networkd`.
  - **Bluetooth:** Hardware initialization (`rgslide-bt-init`), `bluetoothd`, and pairing helper (`rgslide-bt-pair`).
  - **USB:** USB Gadget network (`rndis` / `cdc_ether`) for development and SSH access (`console@10.55.0.2`).
* **System & Integration:**
  - Minimal Debian 13 (Trixie) arm64 base.
  - CM4 coprocessor kept alive (`rgslide-cm4-prep`) to prevent the ~284 s hardware watchdog reboot.
  - Hardware RTC (`sc27xx-rtc`) persistence + automatic SNTP synchronization (`rgslide-clock`) with `/var/lib/rgslide/lasttime` fallback.
  - Dynamic language synchronization (English / Spanish) between ES-DE and the Tools menu; complete persistent timezone selector.

### Known Limitations & Open Issues

* **Kernel Deep Sleep (`mem` / `s2idle`):**
  - Currently handled via instant userspace standby (`rgslide-sleepd`) because stock kernel 5.4 `mem`/`s2idle` hangs in Unisoc PM notifiers (`sprd_pcm_pm_notifier`) when audio/DRM are active.
* **External Video Output (USB-C DisplayPort / HDMI Alt Mode):**
  - Untested with a real external monitor (`eglplatform_drm` / `rgslide-dpd` hotplug hooks exist, but only fallback to the internal panel has been verified; do not rely on DP/HDMI working yet).
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
4. **Stock 5.4.233 Kernel (No Mainline / No Mesa Panfrost GLES):**
   - The system boots the stock Anbernic/Unisoc Android Linux 5.4.233 kernel binary with vendor modules (`mali_kbase`). While **Vulkan 1.3** runs via open-source **Mesa PanVK 26.3** directly on top of stock `/dev/mali0` (`mali_kbase`), **there is NO Mesa Panfrost (OpenGL ES) or mainline kernel yet** — all OpenGL ES workloads (ES-DE, `rgslide-ui`, GLES cores) still rely on `libhybris` + the proprietary Arm Mali userspace blob.

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

The standalone release packages (`.tar.zst`, < 800 MB each) include an auditable flashing script (`flash.sh` / `flash-rgslide.sh`) and stock BROM loaders:

- **Anbernic RG Slide:** `rgslide-linux-brom-v0.9.1.tar.zst`
- **Anbernic RG406H:** `rg406h-linux-brom-v0.9.1.tar.zst`
- **Anbernic RG476H:** `rg476h-linux-brom-v0.9.1.tar.zst`

1. Extract the release `.tar.zst` archive on a Linux x86_64 host and verify `SHA256SUMS`:
   ```bash
   tar -xvf rg*-linux-brom-v0.9.1.tar.zst
   cd rg*-brom-flasher
   ```
2. Preview the exact flash command safely without touching the device:
   ```bash
   ./flash.sh --dry-run   # (or ./flash-rgslide.sh --dry-run)
   ```
3. Power off your console completely (hold `Vol-Down + Power` if frozen).
4. Hold the **Back** button on the device and connect USB-C to your PC.
5. Run the flasher (writes `boot_b`, `misc`, and `userdata`, prompting for `FLASH` confirmation):
   ```bash
   sudo ./flash.sh
   ```

### Iterating During Development (Fastboot -> Slot A)

When testing a newly built `boot-<device>.img` or `rootfs-<device>-v0.9.1.ext4` without touching the known-good Slot B:

```bash
fastboot flash boot_a out/boot-<device>.img
fastboot flash userdata out/rootfs-<device>-v0.9.1.ext4
fastboot reboot
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
  - `pytls`: `rgslide-python3` static TLS-padded CPython 3.13 launcher for PortMaster / PySDL2 on Mali.
  - `ahbdrm` / `drmshow`: Direct DRM/KMS test tools and frame allocators.
  - `spd_dump`, `mkbootimg.py`, `unpack_bootimg.py`, `avbtool.py`, `lpunpack`: Flashing and Android image tools.
- `patches/`: Patches applied to `libhybris`, `sdl2`, `retroarch`, `panvk`, and `es`.
- `scripts/`: Modular build pipeline (`build-rootfs.sh`, `build-libhybris.sh`, `build-panvk.sh`, `build-sdl2-hybris.sh`, `build-esde.sh`, `build-retroarch.sh`, `build-rgslide-python3.sh`, `build-ags.sh`, `build-full-rootfs.sh`, `pack-boot.sh`, `build-initramfs.sh`).
- `docs/`: Reverse-engineering notes, partition layouts, audio codec registers, panic analyses, and bring-up quirks (in Spanish).

---

## Building from Source

The build pipeline cross-compiles the entire userland (Debian 13 Trixie arm64, libhybris, Mesa PanVK, SDL2-hybris, ES-DE, RetroArch, custom daemons) on an x86_64 or aarch64 Linux host PC.

### Prerequisites (Host PC)

- **OS:** Linux x86_64 or aarch64 (Ubuntu 22.04+ or Debian 12+ recommended).
- **Core packages:**
  ```bash
  sudo apt-get install -y build-essential debootstrap qemu-user-static binfmt-support \
    e2fsprogs cmake ninja-build gcc-aarch64-linux-gnu g++-aarch64-linux-gnu \
    device-tree-compiler android-sdk-libsparse-utils python3 python3-mako python3-yaml \
    bison flex glslang-tools docker.io
  ```
- **Android NDK:** NDK r29 or r27 (default search path: `~/Android/Sdk/ndk/29.0.14206865` or set `NDK=/path/to/ndk`).
- **Docker:** Configured with permissions for your current user (`sudo usermod -aG docker $USER`). Docker is used for isolated container builds (`rgslide-build:trixie` and `debian:trixie`).
- **Third-Party Submodules / Clones:**
  Ensure the repositories in `third_party/` are present (see `third_party/SOURCES.md`):
  - `third_party/libhybris`
  - `third_party/android-headers-13`
  - `third_party/panvk-kbase-android`
  - `third_party/ES-DE`
  - `third_party/RetroArch`
  - `third_party/busybox-aarch64`
- **Stock Partition Dumps:**
  A dump of your console's stock partitions in `dump/` (populated via `scripts/dump-min.sh` or BROM dump):
  - `dump/android/root/` (extracted Android vendor/system trees via `scripts/extract-android-bits.sh`)
  - `dump/android/mali/` (`libGLES_mali.so`, `vulkan.ums9620.so`, `gralloc.default.so`)
  - `dump/boot/` (`boot_a.img`, extracted kernel and ramdisk)
  - `dump/live/dlkm/` (stock 5.4.233 kernel modules)
  - `out/wifi-fw/` (Unisoc Marlin3 Wi-Fi/BT firmware blobs)

### Build Pipeline (Step-by-Step)

1. **Build Debian 13 Base Rootfs:**
   ```bash
   ./scripts/build-rootfs.sh --no-image
   ```
   This generates the minimal Debian rootfs tree in `out/rootfs`.

2. **Build libhybris (Android Translation Layer):**
   ```bash
   ./scripts/build-libhybris.sh
   ./scripts/build-libhybris-glvnd.sh
   ```
   Builds the standard and GLVND vendor drivers (`libEGL_libhybris.so`, `libGLESv2_libhybris.so`, `eglplatform_drm.so`) into `out/hybris-root` and `out/hybris-glvnd-root`.

3. **Build Mesa PanVK (Native Vulkan 1.3 Driver):**
   ```bash
   ./scripts/build-panvk.sh
   ```
   Compiles Mesa 26.3 PanVK with Valhall v9 JM (`mali_kbase`), `VK_KHR_display`, and Wayland/X11 WSI support inside a Docker container, outputting `out/panvk-dist/libvulkan_panfrost.so`.

4. **Build SDL2 with hybris DRM Driver (`libudev` + ALSA):**
   ```bash
   ./scripts/build-sdl2-hybris.sh
   ```
   Builds SDL2 2.32.4 patched with the custom `hybris` backend (verifying `HAVE_LIBUDEV_H=1` and `SDL_AUDIO_DRIVER_ALSA=1`) into `out/sdl2-hybris-root`.

5. **Build EmulationStation Desktop Edition (ES-DE):**
   ```bash
   ./scripts/build-esde.sh
   ```
   Compiles ES-DE 3.5.0 with GLES 3.0 support linked against SDL2-hybris, installing into `out/esde-root/opt/es-de`.

6. **Build RetroArch, Libretro Cores & PortMaster Helpers (`rgslide-python3`, `rgslide-ags`):**
   ```bash
   ./scripts/build-retroarch.sh
   ./scripts/build-rgslide-python3.sh
   ./scripts/build-ags.sh
   ```
   Compiles RetroArch 1.22.2 (with native Vulkan, hybris context, and physical rumble bridge), `out/rgslide-python3` (TLS-safe Python 3.13 launcher for PortMaster), and `out/rgslide-ags` (AGS 3.6.3.17 with native OpenGL ES 2.0 on Mali-G57). Extra libretro cores should be placed in `out/extra-cores/` and info files in `out/extra-cores-info/`.

7. **Assemble Final Rootfs Image (Select Device):**
   ```bash
   # For RG Slide (default)
   DEVICE=rgslide ./scripts/build-full-rootfs.sh out/rootfs-rgslide-v0.9.1.ext4

   # For RG406H
   DEVICE=rg406h ./scripts/build-full-rootfs.sh out/rootfs-rg406h-v0.9.1.ext4

   # For RG476H
   DEVICE=rg476h ./scripts/build-full-rootfs.sh out/rootfs-rg476h-v0.9.1.ext4
   ```
   Integrates all components, device-specific overlays (`devices/<device>/overlay/`), firmware, Mali drivers, systemd services (`rgslide-sleep`, `rgslide-ch`, `rgslide-imu`, `rgslide-dp`, etc.), ES-DE configs, and generates the final bootable ext4 filesystem.

8. **Pack Boot Image (Select Device):**
   ```bash
   DEVICE=<device> ./scripts/pack-boot.sh out/boot-<device>.img
   ```

9. **Flash to Testing Slot (Slot A) via Fastboot:**
   ```bash
   fastboot flash boot_a out/boot-<device>.img
   fastboot flash userdata out/rootfs-<device>-v0.9.1.ext4
   fastboot reboot
   ```

---

## Licensing, GPL Source Offer & Proprietary Blobs Disclosure

- **Original code:** Licensed under the **MIT License** — see [`LICENSE`](LICENSE).
- **Third-party attributions & GPLv3 source offer:** See [`NOTICE`](NOTICE) and [`THIRD-PARTY.md`](THIRD-PARTY.md), which include exact upstream commits, local patches, and source tarball links for **RetroArch 1.22.2** (GPLv3), bundled **libretro cores**, **ES-DE 3.5.0**, **libhybris**, **SDL2**, and vendored tools.
- **Pinned tool checksums:** See [`MANIFEST.md`](MANIFEST.md).
- **Proprietary Blobs Disclosure:**
  - This **Git repository** does **not** store proprietary firmware or vendor blobs (`dump/` and `out/` are gitignored).
  - **Prebuilt release archives** (`rgslide-linux-brom-*.tar.zst`) **DO redistribute** proprietary binaries extracted from stock Anbernic RG Slide firmware inside `userdata.ext4` (`/opt/android` including Arm `libGLES_mali.so` ~43 MB, Android vendor/bionic libraries, stock 5.4.233 kernel modules, Unisoc Wi-Fi/BT firmware) as well as `boot.img` and `fdl1`/`fdl2` loaders. These files are redistributed unofficially for personal use and interoperability on RG Slide hardware owned by the user, without an explicit license from Arm, Unisoc, or Anbernic. All rights remain with their respective copyright holders.
