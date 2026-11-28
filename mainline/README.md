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
└── scripts/           # Build, boot-image packing, and Slot A flash helpers
```

## Bring-Up Roadmap & Status

| Milestone | Subsystem / Goal | Status |
| :--- | :--- | :--- |
| **M0 — Minimal Boot** | Linux 6.18 LTS (`6.18.60-rgslide-m0`) + LK stub + stock `vendor_boot` DTB + USB ECM (`10.55.0.2`) & ACM (`/dev/ttyACM0`) + `ramoops`/pstore + 8 CPUs + 8 GiB RAM + `/dev/kvm` | **Done** |
| **M1 — Storage & Net** | UFS (`22000000.ufs`, 128 GB, 73 GPT parts) + Debian 13 Trixie `switch_root` + `systemd` + SSH (`10.55.0.2`) + `/dev/ttyACM0` + Marlin3 SDIO Wi-Fi (`wlan0` via `wcn_bsp` + `sprd_wlan_combo`) & Bluetooth (`/dev/ttyBT0` via `sprdbt_tty`) | **Done** |
| **M2 — Display & GPU** | **Mali-G57 Panfrost (`0x9091`, `/dev/dri/renderD128`, Mesa 25.0.7 OpenGL 3.1 / GLES 3.1)** + **UMS9620 DPU R6P0 KMS (`sprd-drm`, `/dev/dri/card0`, 1280×960 handoff)** + **PWM backlight (`64030000.pwm` + `pwm_bl`)** — verified live with `kmscube` spinning on screen at ~58.5–60 FPS | **Done** |
| **M3 — Console Input** | Integrated gamepad (`retrogame_joypad`), touchscreen (`fts_ts`), Hall lid sensor, volume keys, rumble | Planned |
| **M4 — Full Parity** | UMP9620/AGDSP audio, CM4 watchdog/IMU, battery/charger (`aw32257`), VPU, `s2idle` suspend | Planned |
