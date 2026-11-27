# Contributing to rgslide-linux

Thank you for helping improve Linux on the Anbernic RG Slide (Unisoc T820 / `ums9620_2h10`).

## Slot Safety Rules

This project enforces a strict A/B slot convention:

- **Slot B (`boot_b`)** is reserved for the **known-good / release** image.
- **Slot A (`boot_a`)** is used for **development and testing** via `fastboot`.

Always verify your target slot before flashing (`./tools/flash-rgslide.sh --mode fastboot --slot a --dry-run` or `fastboot getvar current-slot`). Never overwrite Slot B until an image has been verified across multiple cold boots.

## Repository Rules

- **Never commit proprietary dumps or build outputs.** `dump/`, `out/`, `third_party/`, `*.img`, and `*.pac` are gitignored on purpose.
- **Keep `MANIFEST.md` and `THIRD-PARTY.md` up to date** whenever adding or updating a vendored tool, patch, or third-party build dependency.
- **Language:** `README.md` (English) and `README.es.md` (Spanish) are kept in sync. Technical reverse-engineering notes in `docs/` and commit messages may be written in Spanish or English.

## Pre-PR Verification Checklist

Before opening a pull request, run:

```bash
python3 -m compileall -q scripts tools initramfs
bash -n tools/flash-rgslide.sh
for s in scripts/*.sh; do sh -n "$s"; done
```

If you modify any Device Tree Blob (`.dtb`), verify it decompiles cleanly with `dtc -I dtb -O dts`.

## Release Checklist (Maintainers)

1. Use pre-release tags (`v0.9.x-rcN`) until cold-boot stability (`apsys_dvfs`), `s2idle`, and core regressions are resolved.
2. Copy the versioned `tools/flash-rgslide.sh` directly into the release archive — never ship an unversioned flasher.
3. Generate `SHA256SUMS` for the release archive and internal images, and sign it with GPG (`gpg --detach-sign --armor SHA256SUMS`) once a project signing key is published.
4. Include links to `LICENSE`, `NOTICE`, `THIRD-PARTY.md` (GPLv3 source offer for RetroArch/cores and ES-DE), and the Step-0 mandatory backup warning in the GitHub release notes.
