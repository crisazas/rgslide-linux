# PROGRESS

## Ahora
- Fase previa casi cerrada: repos clonados, tools/ listo, .pac V1.46 bajado, FDL1/FDL2 extraidos (dump/fdl), initramfs construido. Rootfs Debian aun en marcha (subagente).

## Siguiente
- Fase 0 minima con la Slide conectada: GPT real + volcar solo prodnv/miscdata/misc/calinv/persist/nr_* (sin respaldo completo, decision del usuario). El .pac es la unica vuelta a Android.
- Reglas udev (90-spd-dump.rules) pendientes de instalar en /etc/udev/rules.d (pide OK).
- Unlock: el repo trae unisoc-unlock, probado SOLO en T618; el T820 puede no aceptar su clave.

## No olvidar
- Subagentes no commitean; el indice lo mueve un agente.
- pac-fdl.py de base618 rechaza ums9620: usar scripts/pac-list.py.
- spd_dump es binario prehecho, sin fuente.
