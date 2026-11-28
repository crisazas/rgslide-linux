# Mainline Kconfig Fragments

Place modular kernel configuration fragments here:

- `ums9620-base.config` — Core SoC, PMIC (`UMP9620`), USB gadget, `pstore`/`ramoops`, and container/DRM baseline.
- `rgslide.config` — Handheld-specific drivers (UFS, DSI/DRM, Panfrost, input, audio, sensors).

Scripts merge these fragments into `out/mainline/` without touching Stage 1 configs.
