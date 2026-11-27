# Third-Party Software, Licenses, and GPL Source Offer

`rgslide-linux` original code (scripts, `rootfs/overlay/`, `tools/` utilities written for this project, `patches/`, and documentation) is licensed under the **MIT License** (see [LICENSE](LICENSE)).

This document lists all third-party components vendored in the repository or bundled in prebuilt release images, their licenses, exact upstream versions/commits, and links to the corresponding source code.

---

## 1. GPLv3 & Copyleft Source Offer (Prebuilt Release Images)

Prebuilt release images (`rgslide-linux-brom-*.tar.zst`) include compiled binaries of **RetroArch** (GPLv3), **libretro cores** (GPLv2/GPLv3/MPL-2.0), **ES-DE** (MIT + third-party components), and **Debian 13 (Trixie)** packages.

In compliance with the GNU General Public License (GPLv2/GPLv3), the exact corresponding source code and all modifications applied by this project are available below:

### RetroArch 1.22.2 (GPLv3)
- **Upstream repository:** https://github.com/libretro/RetroArch
- **Exact commit built:** `ad4e45ca091cdbe5b6d0e3b3b3e6a61fbd6172db` (version `1.22.2`)
- **Exact source tarball:** https://github.com/libretro/RetroArch/archive/ad4e45ca091cdbe5b6d0e3b3b3e6a61fbd6172db.tar.gz
- **Release tag tarball:** https://github.com/libretro/RetroArch/archive/refs/tags/v1.22.2.tar.gz
- **Local patches applied:** [`patches/retroarch/`](patches/retroarch/)
- **Build script:** [`scripts/build-retroarch.sh`](scripts/build-retroarch.sh)

### Bundled libretro Cores
The release image bundles the following libretro cores (installed from Debian Trixie arm64 packages / upstream libretro):

| Core binary | Upstream / Project | License |
|---|---|---|
| `bsnes_mercury_balanced_libretro.so` | libretro/bsnes-mercury | GPLv3 |
| `desmume_libretro.so` | libretro/desmume | GPLv2 |
| `gambatte_libretro.so` | libretro/gambatte-libretro | GPLv2 |
| `mednafen_vb_libretro.so` | libretro/beetle-vb-libretro | GPLv2 |
| `mednafen_wswan_libretro.so` | libretro/beetle-wswan-libretro | GPLv2 |
| `mgba_libretro.so` | mgba-emu/mgba | MPL-2.0 |
| `nestopia_libretro.so` | libretro/nestopia | GPLv2 |

Debian source packages for any Debian-provided binaries in the rootfs can be obtained directly from Debian Trixie (`deb-src https://deb.debian.org/debian trixie main`) or https://snapshot.debian.org/.

### EmulationStation Desktop Edition (ES-DE) 3.5.0 (MIT + third-party)
- **Upstream repository:** https://gitlab.com/es-de/emulationstation-de
- **Exact tag / commit built:** `v3.5.0` (`50e4b600ae533d772bae3ff880d11a09b05dbe84`)
- **Exact source tarball:** https://gitlab.com/es-de/emulationstation-de/-/archive/v3.5.0/emulationstation-de-v3.5.0.tar.gz
- **Local patches applied:** [`patches/es/`](patches/es/)
- **Build script:** [`scripts/build-esde.sh`](scripts/build-esde.sh)

### Written Offer
For any copyleft binary distributed in a release archive where direct upstream links become unavailable, you may open an issue on https://github.com/crisazas/rgslide-linux/issues to obtain the complete corresponding source code and build scripts for at least three years from the release date.

---

## 2. Graphics & Compatibility Stack (Built from Source)

| Component | License | Upstream URL | Commit / Version | Local patches |
|---|---|---|---|---|
| `libhybris` | Apache-2.0 | https://github.com/libhybris/libhybris | `7079712` | [`patches/libhybris/`](patches/libhybris/) |
| `android-headers` | Apache-2.0 | https://github.com/Halium/android-headers | `217a38e` (`halium-13.0`) | — |
| `hybris-patches` | Apache-2.0 | https://github.com/Halium/hybris-patches | `709f929` (`halium-13.0`) | applied to AOSP bionic |
| AOSP `bionic` | BSD / Apache-2.0 | https://android.googlesource.com/platform/bionic | `0b981d1` (`android-13.0.0_r1`+) | via `hybris-patches` |
| `SDL2` | zlib | Debian `libsdl2` / https://github.com/libsdl-org/SDL | `2.32.4` | [`patches/sdl2/`](patches/sdl2/) |

---

## 3. Tools Vendored in This Repository (`tools/`)

| File | Origin | License | Notes |
|---|---|---|---|
| `tools/mkbootimg.py` | AOSP `platform/system/tools/mkbootimg` | Apache-2.0 | Downloaded 2026-11-24; SHA-256 in [MANIFEST.md](MANIFEST.md) |
| `tools/unpack_bootimg.py` | AOSP `platform/system/tools/mkbootimg` | Apache-2.0 | Downloaded 2026-11-24; SHA-256 in [MANIFEST.md](MANIFEST.md) |
| `tools/gki/generate_gki_certificate.py` | AOSP `platform/system/tools/mkbootimg` | Apache-2.0 | Dependency of `mkbootimg.py` |
| `tools/avbtool.py` | AOSP `platform/external/avb` | MIT | Self-reports `avbtool 1.3.0`; SHA-256 in [MANIFEST.md](MANIFEST.md) |
| `tools/lpunpack` | https://github.com/unix3dgforce/lpunpack | See upstream repo | Commit `c59b8f3`; shebang added |
| `tools/spd_dump` | `beebono/rg-rotate-linux` (`94d2d27`) / `TomKing062/action_spd_dump_it` | Closed-source prebuilt binary | Static x86_64 musl build; SHA-256 in [MANIFEST.md](MANIFEST.md). Open-source predecessor: `ilyakurdyukov/spreadtrum_flash` (GPLv2) |

---

## 4. Proprietary Vendor Blobs (Prebuilt Release Images Only)

This Git repository **does not** contain proprietary firmware or vendor blobs (`dump/` and `out/` are gitignored).

However, **prebuilt release archives** (`rgslide-linux-brom-*.tar.zst`) **do include** proprietary binaries extracted from stock Anbernic RG Slide firmware so the image can boot and render on the device out of the box:

- `/opt/android/vendor/lib64/egl/libGLES_mali.so` (~43 MB, Arm Mali-G57 userspace driver) and supporting Android vendor/system libraries (`bionic`, `linker64`, `gralloc`, `servicemanager`, `hwservicemanager`, `vndservicemanager`, `allocator@4.0-service`).
- `/lib/modules/5.4.233/*.ko` (stock Unisoc/Anbernic kernel 5.4.233 modules).
- `/usr/lib/firmware/` (Unisoc Wi-Fi/Bluetooth firmware blobs).
- `boot.img` (stock Linux 5.4.233 kernel binary repacked with custom initramfs) and `fdl1-dl.bin` / `fdl2-dl.bin` (Unisoc BROM flashing loaders).

These files are **not** open source and are redistributed unofficially for interoperability and personal use on Anbernic RG Slide hardware owned by the user, without an explicit redistribution license from Arm, Unisoc, or Anbernic. All rights remain with their respective owners.
