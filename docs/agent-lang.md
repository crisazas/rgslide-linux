# Idioma es/en de los menús (rgslide-ctl, rgslide-ui, EmulationStation) (2026-11-25)

Petición del usuario: «se ve excelente, solo que debe tener el mismo idioma del sistema (adaptarse)». Antes: `rgslide-ctl`/`rgslide-ui` fijos en español, menú Start de ES fijo en inglés.

## 1. De dónde sale el idioma
Una sola regla, igual en las tres piezas (`lang_show` en `rgslide-ctl`, `tools/rgslide-ui/lang.h`, `es-core/src/Lang.cpp` del parche de ES). Gana lo primero que exista:
1. `RGSLIDE_LANG=es|en` en el entorno (pruebas).
2. `/etc/default/rgslide-lang` con `RGSLIDE_LANG=auto|es|en` (lo escribe `rgslide-ctl lang set ...` y el selector «Idioma» del panel). `auto` o ausente: sigue.
3. `LC_ALL`, `LC_MESSAGES`, `LANG` del entorno.
4. `LANG=` de `/etc/default/locale`, luego de `/etc/locale.conf` (ES lo lanza systemd sin `LANG`, así que en la práctica decide este fichero).
5. Nada definido: **es**. `es`, `es_*`, `es.*`, `es@*` → es; cualquier otro valor (`C`, `POSIX`, `en_US...`) → **en**.

Hoy en la Slide (leído por ssh, solo lectura): `/etc/default/locale` = `LANG=en_US.UTF-8`, `locale -a` = `C C.utf8 en_US.utf8 POSIX`, sin `/etc/locale.conf`. **Con `auto` la Slide saldría en inglés.** El usuario habla español: o se instala con `--set-lang es`, o se genera un locale español y se pone en `/etc/default/locale`. Decisión abierta; no se cambió el locale del sistema (solo se lee).

## 2. Mecanismo (sin dependencias nuevas)
- `/usr/share/rgslide/lang/es.tr` y `en.tr` (repo: `rootfs/overlay/usr/share/rgslide/lang/`): líneas `clave=texto`, `#` comentario, UTF-8, sin saltos; `%s`/`%d` en el mismo orden.
- La **clave** es el texto español actual sin acentos (en las filas de ES, el texto inglés original). Si falta la clave se muestra la propia clave: nada se rompe. **Los `.menu` no cambian** salvo una línea nueva en `panel.menu` (selector «Idioma»); el protocolo de `rgslide-ui` (`%s/%d/%p`) está intacto. Los valores de `choice` (`on|off|auto|max`) se traducen solo al dibujar (`< encendido >`); el comando recibe `on`.
- `rgslide-ctl`: `tt`, `tf`, `say`, `sayf`, `die`, `dief`, `row` (etiquetas de estado alineadas aunque tengan acentos). Estado, mensajes y errores traducidos.
- `rgslide-ui`: `lang.h` (`lang_init()`, `tr()`, `lang_cols()` para contar columnas UTF-8): títulos, etiquetas, pie de ayuda, teclado en pantalla, confirmación, «Cargando…», título de `--view`.
- Nombres de los 7 scripts de `~/roms/settings`: `rgslide-ctl lang apply [DIR]` genera `gamelist.xml` con `<name>` traducido (`00 Control panel`…) y `<desc>`; ES lo lee antes que ningún otro. Los `.sh` conservan su nombre. Se ejecuta al instalar y en `lang set`.
- `rgslide-ctl lang [show|get|set auto|es|en|apply [DIR]]` (`--dry-run` no escribe).

## 3. Menú Start de ES: `patches/es/0001-idioma-es-en-menus.patch`
`git apply --check` OK sobre `third_party/EmulationStation`; **no aplicado ni commiteado**. Añade `es-core/src/Lang.{h,cpp}` (+`CMakeLists.txt`) con `_(clave)` y lo engancha en los **puntos comunes**, por lo que traduce todo lo que pasa por ellos: título de menú (`MenuComponent::setTitle`), `addWithLabel`, valores de `OptionListComponent`, botones (`ButtonComponent::setText`), cuerpo de `GuiMsgBox`, **barra de ayuda** (`HelpComponent`), entradas de `GuiMenu::addEntry`, filas RESTART/QUIT EMULATIONSTATION y RESTART/SHUTDOWN SYSTEM, SCRAPE NOW, SCREENSAVER SETTINGS, `GuiDetectDevice` y `GuiInputConfig` («Configure input» completo: 25 nombres de botón, PRESS ANYTHING, -NOT DEFINED-…), nombre de sistema (`fullname`, carrusel de texto).
- **Cubierto**: MAIN MENU, SOUND SETTINGS, UI SETTINGS, OTHER SETTINGS, QUIT, CONFIGURE INPUT, SCRAPER (casi todo) y la barra de ayuda (≈135 claves en `es.tr`, sección «EmulationStation»).
- **Sin traducir** (cae al inglés; el escaneo da 51 claves): menú de opciones de la lista de juegos (`GuiGamelistOptions`: OPTIONS, JUMP TO, SORT GAMES BY, FILTER GAMELIST…, filas con `TextComponent` directo), `GuiCollectionSystemsOptions`, `GuiGeneralScreensaverOptions` y pantallas de salvapantallas (vídeo/slideshow/OMX), `GuiScraperStart/Multi`, `ScraperSearchComponent`, `GuiMetaDataEd`, `GuiGamelistFilter`, `BusyComponent` («WORKING…»), `GuiTextEditPopup`, valores técnicos de AUDIO CARD/DEVICE (`hw`, `PCM`…), `LOG` y el pie «EMULATIONSTATION Vx». Esfuerzo estimado: ~1-2 h, mecánico (envolver ~50 literales con `_()` y añadir claves). Referencias del enfoque estándar: ES-DE (`es-app/resources/locale`, `.po`) y Batocera (`.po` con libintl); aquí se evitó gettext con la tabla `.tr`.
- Límites: ES pasa a mayúsculas solo ASCII, por eso los valores de ES en `es.tr` ya van en MAYÚSCULAS con acentos; la fuente Open Sans Condensed cubre Latin-1 (`fc-query`); si una traducción desborda una fila, ES la recorta. El idioma se lee **al arrancar ES** (cambiarlo exige reiniciar ES). Si el tema carbon usa logos de imagen para «Ajustes», el nombre en el carrusel no cambia (solo el de texto).

## 4. Verificado SIN tocar la Slide
- `rgslide-ctl` (con `wpa_cli` simulado y `RGSLIDE_LANGDIR` del repo), con `LANG=es_ES.UTF-8` / `en_US.UTF-8` / `C`: `status time|bt|power|wifi|all` (`Batería`/`Battery`, `Temp. SoC`/`SoC temp.`, `Wifi sin conexión`/`Wi-Fi not connected`, `Bluetooth encendido (0 conectados)`/`on (0 connected)`, `Memoria … Disco libre`/`Memory … Free disk`, `Encendida`/`Uptime`); `wifi list` (`[guardada]`/`[saved]`, `abierta`/`open`, `Clave de X`/`Password for X`); `wifi status`; `wifi forget` («…te dejaría sin conexión»/«…would leave you offline»); `--dry-run reboot`; `net`; errores de uso. `lang show` → `es`/`en`/`en`(C); `lang get` → `auto`; `lang apply` → `gamelist.xml` válido (7 juegos); `--dry-run lang set es` no escribe; `RGSLIDE_LANG=en` gana a `LANG=es_MX`.
- `rgslide-ui`: compilado **cruzado** (clang del NDK + sysroot trixie, `out/rgslide-ui.lang`; mismo `NEEDED` que el binario de la Slide: `libGLESv2.so.2`, `libSDL2-2.0.so.0`, `libm`, `libc`; `out/rgslide-ui` sin sustituir). Módulo de idioma ejecutado con `qemu-aarch64-static`: `es_MX` → es; `en_US`/`C` → en; vacío → es; `Seguro?` → `¿Seguro?`/`Are you sure?`; clave inexistente → la clave; 133 claves cargadas. **No se ha visto la UI dibujada.**
- ES: los 10 `.cpp` tocados pasan `clang++ -fsyntax-only` (aarch64, C++11) en una copia en `/tmp/opencode/es-lang` (con stubs de `curl.h`/`alsa`); `Lang.cpp` enlazado y ejecutado con qemu (`MAIN MENU` → `MENÚ PRINCIPAL` en es, la clave en en/C). **ES completo NO compilado** (faltan las dependencias de `build-es.sh` en `/tmp/opencode/esdeps`); **menú traducido no visto.**
- Datos: sin claves duplicadas; las etiquetas, títulos y opciones de los 6 `.menu` y los 7 scripts tienen es y en; ninguna traducción supera el ancho de fila de `rgslide-ui`. Sin entrada (cae a la clave): `UTC` y los mensajes de uso de comandos.

## 5. Despliegue (NO ejecutado; esperar a que la Slide esté estable)
```
scripts/install-lang.sh --set-lang es                # ctl + ui + menús + lang/ + gamelist de Ajustes; sin tocar ES
scripts/install-lang.sh --set-lang es --restart-es   # además reinicia SOLO rgslide-es
scripts/install-lang.sh --undo                       # restaura desde *.pre-lang y quita lang/
```
- Sin `--set-lang` instala `auto` (= idioma del sistema; hoy **inglés**); no pisa un `/etc/default/rgslide-lang` existente salvo con `--set-lang es|en`.
- Menú Start de ES: `git -C third_party/EmulationStation apply ../../patches/es/0001-idioma-es-en-menus.patch` (sin commitear), `scripts/build-es.sh`, copiar el binario a `out/es-root-lang/opt/emulationstation/emulationstation` y `scripts/install-lang.sh --es --restart-es`. Sin ES parcheado cambian Ajustes (`rgslide-ui`) y los nombres de los 7 scripts, pero no el menú Start.
- Guarda `.pre-lang` de `rgslide-ui`, `rgslide-ctl` y `emulationstation` la primera vez; no reinicia ni apaga la Slide; si no hay ssh, no toca nada.
- `scripts/push-overlay.sh` ya recoge `lang/`, `ui/` y `rgslide-ctl`; el binario `rgslide-ui` sigue fuera del overlay (pendiente de antes).

## 6. Pendiente
- Ver UI y ES traducidos en pantalla; compilar ES completo con el parche; probar `rgslide-ctl lang set` real (solo `--dry-run`).
- Sin traducir: lista de §3; salida de `bluetoothctl`/`wpa_cli` (inglés de esas herramientas), nombres de redes/dispositivos, `rgslide-fan status` y `rgslide-brightness` (scripts ajenos).
- Decisión abierta: idioma por defecto (el sistema está en `en_US`).
