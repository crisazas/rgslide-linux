# Mainline Build & Flash Scripts

Scripts for building, packing, and testing the mainline kernel:

- All intermediate and final artifacts must go to `out/mainline/`.
- Flash helpers must verify `fastboot getvar current-slot` and write only to **Slot A** (`boot_a`), preserving **Slot B** as the known-good Stage 1 (`v0.9.2`) fallback.
