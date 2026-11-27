# Build & Vendored Binary Manifest

This file pins the exact SHA-256 checksums, upstream sources, and commits of all vendored tools in `tools/` and external source trees used to build `rgslide-linux`.

---

## 1. Vendored Files in `tools/`

| Path | SHA-256 | Upstream Source | Pinned Version / Commit |
|---|---|---|---|
| `tools/avbtool.py` | `e5a664a38db623da00f080219bc0ee60a640a9dc4a872803616fae4938ac749b` | `https://android.googlesource.com/platform/external/avb` | `avbtool 1.3.0` (`main`, fetched 2026-11-24) |
| `tools/mkbootimg.py` | `37d84b3d162e0bc62e36c1f4e1c63c85ea0caa9f29be023eb2f8efe006ad948c` | `https://android.googlesource.com/platform/system/tools/mkbootimg` | `main`, fetched 2026-11-24 |
| `tools/unpack_bootimg.py` | `a9d260978a63bd06a24b6347e7dee8a28ff96639793caea15dff6aa491316308` | `https://android.googlesource.com/platform/system/tools/mkbootimg` | `main`, fetched 2026-11-24 |
| `tools/gki/generate_gki_certificate.py` | `1bb1feec68a13da18d581aa2c631798f86f6bc10b55d587b2dd31446a0f8a203` | `https://android.googlesource.com/platform/system/tools/mkbootimg` | `main`, fetched 2026-11-24 |
| `tools/lpunpack` | `26f3f2c8f577ad27dd309bb9413d5a240888537fead7a75c85cd20b52a3fe78f` | `https://github.com/unix3dgforce/lpunpack` | `c59b8f3` (+ `#!/usr/bin/env python3` shebang) |
| `tools/spd_dump` | `5740b3815b6d16f4d6ab37d101544c12feb5f751069c98cafd0adca010be721b` | `https://github.com/beebono/rg-rotate-linux` (`tools/spd_dump/spd_dump`, from `TomKing062/action_spd_dump_it`) | `beebono/rg-rotate-linux@94d2d273c20aeabec2b4c6b160e33e0c2d26dd41` (static x86_64 musl binary) |

Verify locally with:

```bash
sha256sum -c <<'EOF'
e5a664a38db623da00f080219bc0ee60a640a9dc4a872803616fae4938ac749b  tools/avbtool.py
37d84b3d162e0bc62e36c1f4e1c63c85ea0caa9f29be023eb2f8efe006ad948c  tools/mkbootimg.py
a9d260978a63bd06a24b6347e7dee8a28ff96639793caea15dff6aa491316308  tools/unpack_bootimg.py
1bb1feec68a13da18d581aa2c631798f86f6bc10b55d587b2dd31446a0f8a203  tools/gki/generate_gki_certificate.py
26f3f2c8f577ad27dd309bb9413d5a240888537fead7a75c85cd20b52a3fe78f  tools/lpunpack
5740b3815b6d16f4d6ab37d101544c12feb5f751069c98cafd0adca010be721b  tools/spd_dump
EOF
```

---

## 2. External Source Repositories (`third_party/`, gitignored)

| Local directory | Upstream URL | Pinned Commit / Tag |
|---|---|---|
| `third_party/ES-DE` | `https://gitlab.com/es-de/emulationstation-de.git` | `v3.5.0` (`50e4b600ae533d772bae3ff880d11a09b05dbe84`) |
| `third_party/RetroArch` | `https://github.com/libretro/RetroArch.git` | `ad4e45ca091cdbe5b6d0e3b3b3e6a61fbd6172db` (`1.22.2`) |
| `third_party/libhybris` | `https://github.com/libhybris/libhybris.git` | `7079712` |
| `third_party/android-headers-13` | `https://github.com/Halium/android-headers.git` | `217a38e` (`halium-13.0`) |
| `third_party/hybris-patches-13` | `https://github.com/Halium/hybris-patches.git` | `709f929` (`halium-13.0`) |
| `third_party/bionic-13` | `https://android.googlesource.com/platform/bionic` | `0b981d1` (`android-13.0.0_r1`+) |
| `third_party/rg-rotate-linux` | `https://github.com/beebono/rg-rotate-linux.git` | `94d2d273c20aeabec2b4c6b160e33e0c2d26dd41` |
| `third_party/base618` | `https://github.com/thwonp/base618.git` | `a000e85cec148a09ff30fdb7c0e4fff458f1b7f1` (`v0.4`) |
| `third_party/UnisocBypass` | `https://github.com/TheGammaSqueeze/UnisocBypass.git` | `1d046241ec2c0e96cf3c55c7e18e88e74acbe2da` |
| `third_party/mu300-linux` | `https://github.com/dikeckaan/mu300-linux.git` | `833616a6c03a7c0245300c5946d98ec98b09bb94` |

---

## 3. Release Artifact Verification

Each release archive should be published alongside:
- `SHA256SUMS` covering the `.tar.zst` archive and internal image payloads.
- A detached GPG signature (`SHA256SUMS.asc`) once a release signing key is published in this repository.
