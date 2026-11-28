# Mainline Device Trees & Overlays

Milestone 0 boots using the stock `vendor_boot` DTB unchanged (following the `mu300-linux` approach).

Custom `.dts` / `.dtso` sources for UFS, MIPI-DSI panel (`FT8201P` / `RG406H` / `RG476H`), Panfrost GPU, input devices, and audio will be placed here once required. Always validate compiled DTBs with `dtc -I dtb -O dts`.
