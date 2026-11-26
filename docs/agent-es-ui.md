# Interfaz de EmulationStation: verificacion visual (2026-11-26)

## Como se capturo
`tools/esshot/esshot.c` (LD_PRELOAD tras `libtlspad`) intercepta `SDL_GL_SwapWindow`, hace `glReadPixels` y guarda PPM; ademas lee un guion en `/tmp/es-script` (`wait`, `shot`, `btn`, `hat`) e inyecta eventos del mando. Se compila con el clang del NDK (ver comando en el propio fichero de cabecera / `scripts/build-es.sh`). Uso: parar `rgslide-es`, lanzar `emulationstation` con `LD_PRELOAD=/opt/hybris-glvnd/libtlspad.so:/tmp/esshot.so` y volver a `systemctl start rgslide-es` EN EL MISMO COMANDO (ES de prueba = pantalla ocupada y sin mando). Solo una ejecucion corta.

## Lo visto
- Vista de sistemas: `docs/img/es-sys1.png` (NDS / Game Boy / GBC, "3 GAMES AVAILABLE"), `es-sys4.png` (GBA / NES / NDS), `es-s1.png` (NES / NDS / GB). Fondo de fibra de carbono, carrusel con logos de color, contorno rojo del mando de cada consola, barra de ayuda abajo (fuente Cabin/OpenSans condensada, legible). Hay una foto del usuario en la Slide fisica que lo confirma.
- Lista de juegos NDS: `docs/img/es-list-a.png` (`es-list-b.png` igual con la lista bajada): logo NDS y mando arriba, "DELVE" en rojo, fecha/genero/desarrollador/descripcion abajo a la derecha, barra de ayuda con Y=favoritos, X=random. El hueco de la derecha (caratula) estaba VACIO en esta captura: ver "Arreglado".
- Menu (Start): `docs/img/es-menu.png`, Main Menu en INGLES (Scraper, Sound, UI, Game Collection, Other, Configure Input, Quit), v2.12.0RP-DEV.
- `es-s0.png`, `es-s2.png`: otras posiciones del carrusel.

## Comprobado sin pantalla
- El tema carbon trae logo (`<sys>/art/system.svg`) y mando (`controller.svg`) para gb, gbc, gba, nes, snes, nds, virtualboy, wonderswan. Los avisos `art/logo/${system.theme}.svg` del log son de `theme.xml` raiz, que carbon sustituye con el `theme.xml` de cada sistema: inofensivos. Los logos que se ven son los de color (Game Boy, GBC, GBA, NES, NDS).
- Los sistemas sin ROM (snes, virtualboy, wonderswan) se ocultan: correcto. No hay `auto-lastplayed`/favoritos visibles en el carrusel porque `CollectionSystemsAuto=""`.

## Arreglado
- Gamelists con nombre legible, descripcion, genero, autor, ano y caratula generada (placeholder liso con titulo y consola, sin arte con copyright) para las 9 ROMs. Generador: `scripts/gen-es-gamelists.py` -> `rootfs/overlay/usr/share/rgslide/gamelists/<sys>/` (gamelist.xml + images/). Copiado a `~/.emulationstation/gamelists/` de la Slide (copia previa en `/tmp/gamelists.antes` de la Slide, ahi estaban playcount/lastplayed de las partidas; el nuevo gamelist no los lleva).
- Las captura `es-list-a.png` se hizo con `<image>./images/..`, que ES resuelve respecto a `~/roms/<sys>` (no existe) -> sin caratula. Ahora la ruta es `~/.emulationstation/gamelists/<sys>/images/<juego>.png` (ES expande `~`). NO verificado visualmente tras el cambio.

## Pendiente
- Ver las caratulas tras reiniciar ES (el ES en marcha tiene los gamelists antiguos en memoria y con `SaveGamelistsMode=on exit` puede reescribirlos al salir: reiniciar ES desde el menu Quit > Restart antes de jugar, o recomprobar).
- Menu en ingles (ES clasico sin traduccion a es), sin salvapantallas probado (`ScreenSaverBehavior=dim`, 5 min), favoritos: Y los marca, no probado.
