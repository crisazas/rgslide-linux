# Salida de EmulationStation (2026-11-26)

## Problema
Al elegir "QUIT EMULATIONSTATION" no pasaba nada visible: ES libera la pantalla y el PCM, termina, el bucle de `rgslide-es` sale con codigo 0 y el servicio queda `inactive` con la ultima imagen congelada. No hay consola en pantalla (sin fbcon ni X, docs/agent-control.md), asi que no se podia ni leer el error.

## Que se hace
- `usr/local/bin/rgslide-es`: si ES sale SIN `/tmp/es-restart`, `/tmp/es-sysrestart` ni `/tmp/es-shutdown` (es decir, por QUIT), lee `EXIT_MODE` de `/etc/default/rgslide-es`:
  - `boot-menu` (por defecto): arranca `rgslide-bootmenu.service` y termina. Ese servicio ejecuta `rgslide-bootmenu`, que abre **`/dev/ttyGS0` (serie del gadget USB, 115200)** y lanza el menu de rescate `rgslide-ctl`. Desde ahi: `systemctl start rgslide-es` para volver a los juegos.
  - `restart`: `systemctl restart rgslide-es` (ES vuelve a salir solo; alternativa si el menu por serie no gusta).
  - vacio: comportamiento anterior (se queda la imagen).
- `usr/local/bin/rgslide-bootmenu`, `etc/systemd/system/rgslide-bootmenu.service` (`Conflicts=rgslide-es.service`), `etc/default/rgslide-es`.
- Reiniciar ES, Reiniciar sistema y Apagar del menu **no cambian**: sus marcas en `/tmp` se atienden antes de mirar `EXIT_MODE`.

## Despliegue y vuelta atras
- `scripts/install-es-exit.sh install` (o `undo`). No reinicia ES: los cambios entran en el siguiente reinicio de ES.
- Vuelta atras fina sin reinstalar nada: poner `EXIT_MODE=` (vacio) en `/etc/default/rgslide-es`; o `restart` si solo molesta la consola serie.
- `undo` borra unidad, `/etc/default/rgslide-es`, `rgslide-bootmenu` y hace `daemon-reload`; el `rgslide-es` con el `case` sigue funcionando sin `EXIT_MODE` (cae en `*)` = break).

## Estado
- Escrito y con `sh -n`/`bash -n` correctos. **NO desplegado ni probado**: el 26-11 la Slide quedo sin SSH tras el primer `s2idle` (docs/agent-sleep.md) y espera reinicio a mano. Sin probar: que `ttyGS0` exista y acepte la redireccion, que el menu de `rgslide-ctl` se vea por la serie, y que `systemctl start --no-block` no choque con `Conflicts` al rearrancar ES.
