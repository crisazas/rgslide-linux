# PROGRESS

## Ahora
- Verificar que rgslide-cm4 (systemd) arranca el CM4 solo tras reiniciar y que la Slide supera 15-20 min sin corte; reinicio pedido a las 03:45:47 (sin comprobar aun).

## Siguiente
- Arreglar /dev (sin devtmpfs: /dev/null 660, dbus/resolved/wpa_supplicant fallan, systemd degraded)
- /dev/sctl_ch da ENODEV en bucle (mayor dinamico 498); CM4 Version no visto
- Reflashear rootfs corregido por BROM (el overlay solo esta en la Slide por ssh)
- Sin probar por persona: pantalla/giro/entrada en Debian; GSI: ver docs/quirks.md

## No olvidar
- Subagentes no commitean. Dump/out no se versionan.
