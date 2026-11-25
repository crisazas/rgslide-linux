# rgslide — Linux en la Anbernic RG Slide (T820, ums9620_2h10)

Solo Linux. No queda Android de respaldo: la vuelta atrás es el volcado de la fase 0 restaurado por BROM.
Plan completo: `PLAN.md`. Estado: `PROGRESS.md`.

## Dispositivo
- Anbernic RG Slide, SoC Unisoc T820 = `ums9620_2h10`. Sin microSD: todo por USB (`fastboot` para iterar, `spd_dump`/BROM solo para `uboot` y rescate).
- Entrar en BROM: botón **Back del lado derecho de la pantalla** mantenido al conectar el USB (por confirmar). Forzar apagado: **Vol-down + Power**.
- Slot A = pruebas, slot B = último arranque bueno.

## Rutas
- `third_party/` clones (sin push): `rg-rotate-linux`, `base618`, `UnisocBypass`, `mu300-linux`.
- `dump/` volcado de particiones y FDL. `out/` artefactos. Los dos van en `.gitignore`, no se commitean.
- Herramientas del SDK: `~/Android/Sdk/platform-tools/fastboot`, `~/Android/Sdk/build-tools/37.0.0`.

## Reglas
- El proyecto vive dentro del repo del home (`/home/console`), que solo versiona config: aquí hay `.git` propio y `rgslide/` está en el `.gitignore` del home. **Nunca** `git add` desde el home.
- Los subagentes escriben, pero **no commitean**: el índice de git lo mueve un solo agente.
- Antes de escribir en el equipo, `fastboot getvar current-slot` y confirmar que se escribe en A.
- Verificación: `python3 -m compileall -q scripts tools initramfs` si hay `*.py`; `dtc -I dtb -O dts` sobre cualquier DTB que se toque.
- `sudo` sin contraseña; instalar paquetes pide OK.
