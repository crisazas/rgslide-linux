# Mainline Bring-Up Initramfs

Minimal `initramfs` dedicated to mainline bring-up (separate from the Stage 1 `initramfs/` at the repository root).

Milestone 0 goals:
- Mount `/proc`, `/sys`, `/dev`, `/sys/fs/pstore`.
- Configure USB gadget networking (RNDIS/NCM) for host SSH/telnet/shell access.
- Expose `dmesg` and `console-ramoops-0` for debugging without touching `userdata`.
