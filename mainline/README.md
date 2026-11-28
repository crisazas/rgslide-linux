# rgslide-linux — Mainline Kernel Bring-Up (Stage 2)

This directory contains the **mainline Linux kernel** bring-up for Anbernic Unisoc T820 (`ums9620_2h10`) handhelds (**RG Slide**, **RG406H**, **RG476H**).

All Stage 1 (stock kernel `5.4.233` + `libhybris` + PanVK) scripts, patches, initramfs, and rootfs definitions at the repository root remain untouched so that `v0.9.x` releases stay fully reproducible.

## Target Kernel

- **Primary target:** **Linux 6.18 LTS** (supported upstream through December 2027), starting from the validated `ums9620` patchset in `third_party/mu300-linux/upstream/`.
- **Why 6.18 LTS:** Stable internal kernel APIs during out-of-tree driver bring-up (UFS, DSI, Panfrost, input, audio), proven `ums9620` boot path (stock `vendor_boot` DTB + LK load-offset stub), and full support for modern DRM/Panfrost, `drm_syncobj`, `binderfs`, and Linux container namespaces (`PID_NS`/`USER_NS`).
- **Forward compatibility:** Version-specific patches live under `patches/<major.minor>/` so moving to a newer LTS (e.g., `7.x`) remains mechanical once hardware parity is reached.

## Isolation & Safety Rules

1. **Build outputs:** All mainline build artifacts must be written under `out/mainline/` (never overwriting Stage 1 artifacts in `out/`).
2. **A/B slot discipline:**
   - **Slot B** stays reserved for the last known-good Stage 1 (`v0.9.2`) release.
   - **Slot A (`boot_a`)** is used exclusively for mainline bring-up tests. Always confirm `fastboot getvar current-slot` before flashing.
3. **Stock `vendor_boot` first:** Milestone 0 reuses the stock `vendor_boot` DTB unchanged. Do not modify `vendor_boot_a` until custom DT nodes/overlays are required.
4. **Protect `userdata`:** Milestone 0 runs entirely from initramfs (no `userdata` mount). Early UFS bring-up must mount `userdata` read-only (`ro`) first to avoid corrupting the Debian 13 rootfs shared with Slot B.

## Directory Layout

```text
mainline/
├── README.md          # Bring-up overview, rules, and subsystem status
├── config/            # Kconfig fragments (ums9620 base + rgslide overrides)
├── dts/               # Custom device trees / overlays (post-Milestone 0)
├── initramfs/         # Minimal bring-up initramfs (busybox + USB gadget + pstore)
├── modules/           # Temporary out-of-tree modules during bring-up
├── patches/           # Kernel patches (common/ and <major.minor>/)
├── scripts/           # Build, boot-image packing, and Slot A flash helpers
└── systemd/           # Stage 2 module autoload, usb0 IP, and Stage 1 drop-in isolation
```

## Bring-Up Roadmap & Status

| Milestone | Subsystem / Goal | Status |
| :--- | :--- | :--- |
| **M0 — Minimal Boot** | Linux 6.18 LTS (`6.18.60-rgslide-m0`) + LK stub + stock `vendor_boot` DTB + USB ECM (`10.55.0.2`) & ACM (`/dev/ttyACM0`) + `ramoops`/pstore + 8 CPUs + 8 GiB RAM + `/dev/kvm` | **Done** |
| **M1 — Storage & Net** | UFS (`22000000.ufs`, 128 GB, 73 GPT parts) + Debian 13 Trixie `switch_root` + `systemd` + SSH (`10.55.0.2`) + `/dev/ttyACM0` + Marlin3 SDIO Wi-Fi (`wlan0` via `wcn_bsp` + `sprd_wlan_combo`) & Bluetooth (`/dev/ttyBT0` via `sprdbt_tty`) | **Done** |
| **M2 — Display & GPU** | **Mali-G57 Panfrost (`0x9091`, `/dev/dri/renderD128`, Mesa 25.0.7 OpenGL 3.1 / GLES 3.1)** + **UMS9620 DPU R6P0 KMS (`sprd-drm`, `/dev/dri/card1`, 1280×960 handoff)** + **PWM backlight (`64030000.pwm` + `pwm_bl`)** — verified live with `kmscube` spinning on screen at ~58.5–60 FPS | **Done** |
| **M3 — Console Input** | Integrated gamepad (`rgslide_joypad.ko` -> `retrogame_joypad` + `/dev/input/js0`: 18 GPIO buttons + 6 SPI MCU analog axes on `spi0.0`, auto-calibrated Hall triggers), FT8201P touchscreen (`edt-ft5x06.ko` on `i2c-3` `0x38`), Hall slider & volume/power keys (`gpio-keys`), rumble (`sc27xx:vibrator`), plus `sc27xx-efuse0` + `sc27xx-adc` + IIO thermal zones | **Done** |
| **M4 — Full Parity** | **Done & verified on hardware:** **Internal speaker audio** (`sprdphone-sc2730` + AGDSP `l_agdsp_a` + VBC v4 + UMP9620 codec + AGCP DMA + SIPC/mailbox, 440 Hz stereo 48 kHz verified live by ear), **VRR 60 / 120 Hz** (`120.01 fps` via standard DRM KMS stop→reprogram→run on DPU R6P0 + DSI `VIDEO_VBLK_LINES`), **PWM fan** (`pwmfan` on `hwmon1`), battery fuel gauge (`sc27xx-fgu`) + charger (`aw32257`), `s2idle` suspend + `/dev/rtc0` wake, Stage 1/2 systemd isolation (`ConditionKernelVersion=<6.0`), Stage 2 module + audio autoload (`rgslide-mainline-modules.service` + `rgslide-mainline-audio.service` + persistent `usb0` `10.55.0.2/24`), UMP9620 PMIC watchdog verified stable without CM4, Linux 6.18 SIPC + `unisoc-mailbox` + Trusty TEE + `sprd_modem_loader` (`/dev/chsys`, `/dev/pmsys`). **Remaining:** CM4 `sprd-sensorhub` IMU client over SIPC dst 9 (VPU has no upstream V4L2 driver; libretro uses SW decode) | **Done (Core)** |
