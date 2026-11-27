# rgslide-linux: Linux nativo en consolas Anbernic Unisoc T820 (RG Slide, RG406H, RG476H)

> **Idioma:** [English (README.md)](README.md) | Español
> *(Las notas técnicas en `docs/` y el historial de commits se mantienen en español.)*

> **Estado: Pre-release experimental (`v0.9.1`) — NO es una versión `1.0` estable.**
> La estabilidad de arranque aún requiere validación (una condición de carrera entre los módulos de fábrica `apsys_dvfs` y `mmdvfs` causó 2 panics en 14 arranques medidos; existe un `softdep` mitigador con verificación en frío aún limitada), la suspensión profunda (`s2idle`) está restringida y la salida DisplayPort por USB-C aún no funciona.

Distribución Linux nativa (Debian 13 Trixie arm64) para consolas portátiles con procesador **Unisoc T820 (`ums9620_2h10`)** de Anbernic (**RG Slide**, **RG406H** y **RG476H**), reemplazando Android por completo con aceleración 3D por hardware (Mesa PanVK Vulkan 1.3 nativo + GPU Mali-G57 vía `libhybris`), EmulationStation Desktop Edition (ES-DE), RetroArch, audio, Wi-Fi, Bluetooth, pantallas táctiles y controles físicos completos.

---

## Dispositivos soportados

| Dispositivo | Pantalla | Refresco | Formato y Controles | Perfil |
| :--- | :--- | :--- | :--- | :--- |
| **Anbernic RG Slide** | 4.7" 1280x960 | Hasta 120 Hz | Deslizador con sensor Hall (`KEY_WAKEUP`), `retrogame_joypad` | `devices/rgslide` |
| **Anbernic RG406H** | 4.0" 960x720 | 60 Hz | Horizontal, sticks/gatillos Hall, LEDs RGB en sticks (`rg406h-leds`) | `devices/rg406h` |
| **Anbernic RG476H** | 4.7" 1280x960 | Hasta 120 Hz | Horizontal, sticks/gatillos Hall, multiplexor AMUX | `devices/rg476h` |

---

## Estado del proyecto: qué funciona y limitaciones conocidas

### Funcionando

* **Pantalla y aceleración 3D (GPU Mali-G57):**
  - Panel nativo (1280x960) con orientación correcta y hasta 120 Hz.
  - **Vulkan 1.3 (Linux nativo):** Driver libre **Mesa PanVK 26.3** (`libvulkan_panfrost.so`) comunicándose directo con `/dev/mali0` (`mali_kbase` JM v9) y presentación `VK_KHR_display` zero-copy sobre `/dev/dri/card0` (60/90/120 Hz), sin `libhybris`, sin `tlspad` y sin blobs de Android.
  - **OpenGL ES 2.0 / 3.x:** Aceleración mediante `libhybris` + blob Mali de fábrica + `libtlspad.so`.
  - **ES-DE 3.5.0** y **RetroArch 1.22.2** corriendo nativos (RetroArch soporta tanto Vulkan + shaders `.slang` por `khr_display` como OpenGL ES).
* **Audio:**
  - Altavoces estéreo y jack de 3.5 mm vía códec Unisoc UMP9620 (`snd_soc_sprd_codec_ump9620`).
  - Ordenación `softdep` de reguladores (`rgslide-audio.conf`) mitigando estática/chasquidos (`DPOP`) al arrancar.
  - Demonio de mezcla ALSA (`rgslide-audiod`) para sonidos de interfaz y juegos.
* **Controles, deslizador y modo reposo (standby):**
  - Mando integrado (`retrogame_joypad`, `/dev/input/event3`) mapeado y calibrado.
  - **Pantalla táctil (`fts_ts`):** Soporte multi-touch nativo integrado en RetroArch (`UDEV_TOUCH_SUPPORT`), filtrando el nodo fantasma `virtual_gamepad` y enlazando los toques a dispositivos de puntero (`RETRO_DEVICE_POINTER`, lápiz táctil de NDS, pistolas de luz y navegación en menús XMB).
  - Vibración (rumble) nativa en juegos: el driver `udev` de RetroArch está parcheado para enlazar `retrogame_joypad` con `sc27xx:vibrator` (16 efectos FF) sin demonios adicionales.
  - Botones físicos de volumen (`/dev/input/event0`) integrados con `rgslide-hotkeyd` y `rgslide-volume`.
  - Sensor Hall del deslizador (`KEY_WAKEUP`) y pulsación corta del botón Power activan reposo instantáneo (`rgslide-sleepd`): apaga retroiluminación (`brightness=0`), pausa (`SIGSTOP`) ES-DE y RetroArch a mitad de juego y reduce las CPUs a `powersave`; al abrir o volver a pulsar reanuda de inmediato.
  - Sensores de movimiento: el sensorhub del CM4 (`rgslide-ch`) alimenta un servidor UDP DSU/Cemuhook bajo demanda (`rgslide-imud`, `127.0.0.1:26760`) con datos del acelerómetro; coste de CPU nulo en reposo. El canal del giroscopio aún da ceros (el handle 10 no está identificado).
  - DisplayPort USB-C: `eglplatform_drm` prefiere un conector DP/HDMI conectado sobre el panel interno y `rgslide-dpd` gestiona el hotplug. **Sin probar con un monitor real** (solo se verificó el retorno al panel interno).
  - Menú rápido en pantalla (`rgslide-ui`) accesible con `Back + Select`.
* **Conectividad:**
  - **Wi-Fi:** 2.4 / 5 GHz con DHCP automático (`systemd-networkd`).
  - **Bluetooth:** Inicialización (`rgslide-bt-init`), `bluetoothd` y asistente de emparejamiento (`rgslide-bt-pair`).
  - **USB:** Red USB Gadget (`rndis` / `cdc_ether`) para SSH y desarrollo (`console@10.55.0.2`).
* **Sistema:**
  - Base Debian 13 (Trixie) arm64 mínima.
  - Coprocesador CM4 mantenido vivo (`rgslide-cm4-prep`) para evitar el reinicio por watchdog a los ~284 s.
  - Persistencia del reloj RTC (`sc27xx-rtc`) + sincronización automática SNTP (`rgslide-clock`) con respaldo en `/var/lib/rgslide/lasttime`.
  - Sincronización dinámica de idioma (inglés / español) entre ES-DE y el menú Tools.

### Limitaciones conocidas y deuda abierta

* **Carrera de módulos en arranque (`apsys_dvfs` vs `mmdvfs`):**
  - Ambos módulos de fábrica registran el gobernador `vdsp_dvfs`; si `mmdvfs` gana la carrera, `apsys_dvfs` falla sin limpiar y el reintento produce un panic use-after-free (medido en 2 de 14 arranques antes del parche; ver [`docs/agent-panic.md`](docs/agent-panic.md)). Se desplegó `softdep mmdvfs pre: apsys_dvfs`, pendiente de más ciclos de arranque en frío.
* **Suspensión profunda a nivel de kernel (`mem` / `s2idle`):**
  - Implementada mediante reposo instantáneo en espacio de usuario (`rgslide-sleepd`), ya que la suspensión del kernel stock 5.4 (`mem`/`s2idle`) se cuelga en los callbacks de gestión de energía de Unisoc (`sprd_pcm_pm_notifier`) al tener la pila de audio y DRM activa.
* **Salida de vídeo externa (USB-C DisplayPort / HDMI Alt Mode):**
  - Sin implementar todavía.
* **Mezcla ALSA:**
  - Enrutada actualmente a través de `rgslide-audiod` en lugar de mezcla ALSA multicliente directa.

---

## Deuda técnica y compromisos de arquitectura

Antes de flashear o contribuir, ten en cuenta los mecanismos frágiles sobre los que opera actualmente la pila gráfica:

1. **Parche frágil de ABI TLS (`libtlspad.so`):**
   - `tools/tlspad/` reserva un bloque `__thread char[512]` alineado a 64 bytes al inicio del TLS estático para evitar colisiones con los offsets de TLS de Bionic que `libGLES_mali.so` accede directamente (`[tp+0x00..0x70]`). Depende estrictamente del orden de `LD_PRELOAD`; tal como advierte [`docs/quirks.md`](docs/quirks.md), la regla TLS **no es fiable** frente a cambios de orden o nuevas bibliotecas.
2. **Cuatro servicios Android vivos bajo `linker64`:**
   - `servicemanager`, `vndservicemanager`, `hwservicemanager` y `android.hardware.graphics.allocator@4.0-service` deben seguir activos bajo `linker64` (uid `1000`) para que el blob Mali asigne buffers `gralloc`.
3. **Límite de un único DRM master:**
   - Solo un proceso puede poseer DRM master a la vez. ES-DE se compila con `DEINIT_ON_LAUNCH=ON` para liberar DRM antes de lanzar RetroArch o `rgslide-ui`.
4. **Kernel 5.4.233 de fábrica (sin mainline ni Panfrost):**
   - Se utiliza el kernel Android 5.4.233 de fábrica con módulos propietarios de Unisoc/Anbernic; aún no hay soporte mainline ni Panfrost.

---

## Credenciales por defecto y superficie de red

Las imágenes precompiladas incluyen valores de fábrica pensados para desarrollo:

- **Usuario:** `console` / contraseña `console` (con `sudo`)
- **Root:** contraseña `root` (`PermitRootLogin prohibit-password` en `sshd_config`)
- **Servicios activos al arrancar:** `sshd` y red USB RNDIS (`10.55.0.2`).

**Recomendación:** Cambia las contraseñas de `console` y `root` tras el primer arranque (`passwd` y `sudo passwd root`) y configura autenticación por clave SSH si vas a conectar la consola a redes Wi-Fi no confiables.

---

## Guía de flasheo

### **PASO 0 — COPIA DE SEGURIDAD OBLIGATORIA ANTES DE FLASHEAR**

**Flashear `rgslide-linux` sobrescribe `userdata` y reemplaza Android/GammaOS por completo. NO hay arranque dual.**

**Antes de ejecutar el flasher en una consola de fábrica, DEBES volcar tus particiones originales por BROM** (usando `scripts/dump-min.sh` o `tools/spd_dump`), o asegurarte de tener el firmware oficial de fábrica de Anbernic desde la [página oficial de descargas de Anbernic](https://win.anbernic.com/download/) para poder restaurarla por BROM. **Sin el volcado de fase 0 ni el firmware oficial, no hay vuelta atrás a Android/GammaOS.**

### Convención unificada de slots A/B

Este proyecto mantiene una única convención de slots en todo el repositorio (`README.md`, `README.es.md`, `AGENTS.md`, `tools/flash-rgslide.sh` y las releases):

- **Slot B (`boot_b`):** **Último arranque bueno / instalación de release.** Los paquetes de release BROM escriben `boot_b`, `misc` (apuntando al Slot B) y `userdata`.
- **Slot A (`boot_a`):** **Pruebas e iteración de desarrollo.** Al compilar y probar cambios por `fastboot`, se escribe en Slot A para preservar el Slot B bueno.

### Flashear una release precompilada (BROM -> Slot B)

Los paquetes de release BROM autónomos (`.tar.zst`, menos de 800 MB cada uno) incluyen el script de flasheo auditable (`flash.sh` / `flash-rgslide.sh`) y cargadores BROM de fábrica:

- **Anbernic RG Slide:** `rgslide-linux-brom-v0.9.1.tar.zst`
- **Anbernic RG406H:** `rg406h-linux-brom-v0.9.1.tar.zst`
- **Anbernic RG476H:** `rg476h-linux-brom-v0.9.1.tar.zst`

1. Descomprime el archivo `.tar.zst` en un PC Linux x86_64 y verifica `SHA256SUMS`:
   ```bash
   tar -xvf rg*-linux-brom-v0.9.1.tar.zst
   cd rg*-brom-flasher
   ```
2. Previsualiza exactamente qué se va a escribir sin tocar la consola:
   ```bash
   ./flash.sh --dry-run   # (o ./flash-rgslide.sh --dry-run)
   ```
3. Apaga tu consola por completo (`Vol-Down + Power` si estuviera colgada).
4. Mantén pulsado el botón **Back** y conecta el cable USB-C al PC.
5. Ejecuta el flasher (escribe `boot_b`, `misc` y `userdata`, pidiendo confirmación `FLASH`):
   ```bash
   sudo ./flash.sh
   ```

### Iterar en desarrollo (Fastboot -> Slot A)

Para probar una nueva imagen compilada sin tocar el Slot B de respaldo:

```bash
fastboot flash boot_a out/boot-<dispositivo>.img
fastboot flash userdata out/rootfs-<dispositivo>-v0.9.1.ext4
fastboot reboot
```

---

## Compilación desde el código fuente

El flujo de construcción permite la compilación cruzada completa de todo el espacio de usuario (Debian 13 Trixie arm64, libhybris, Mesa PanVK, SDL2-hybris, ES-DE, RetroArch y demonios propios) en un PC host x86_64 o aarch64.

### Requisitos previos (PC Host)

- **Sistema operativo:** Linux x86_64 o aarch64 (Ubuntu 22.04+ o Debian 12+ recomendado).
- **Paquetes base:**
  ```bash
  sudo apt-get install -y build-essential debootstrap qemu-user-static binfmt-support \
    e2fsprogs cmake ninja-build gcc-aarch64-linux-gnu g++-aarch64-linux-gnu \
    device-tree-compiler android-sdk-libsparse-utils python3 python3-mako python3-yaml \
    bison flex glslang-tools docker.io
  ```
- **Android NDK:** NDK r29 o r27 (ruta de búsqueda por defecto: `~/Android/Sdk/ndk/29.0.14206865` o definir `NDK=/ruta/al/ndk`).
- **Docker:** Configurado con permisos para tu usuario actual (`sudo usermod -aG docker $USER`). Docker se utiliza para aislar compilaciones cruzadas en contenedores (`rgslide-build:trixie` y `debian:trixie`).
- **Módulos / clones de terceros:**
  Asegúrate de contar con los repositorios en `third_party/` (ver `third_party/SOURCES.md`):
  - `third_party/libhybris`
  - `third_party/android-headers-13`
  - `third_party/panvk-kbase-android`
  - `third_party/ES-DE`
  - `third_party/RetroArch`
  - `third_party/busybox-aarch64`
- **Volcados de particiones de fábrica:**
  Un volcado de las particiones de fábrica de tu propia RG Slide en `dump/` (obtenido mediante `scripts/dump-min.sh` o volcado BROM):
  - `dump/android/root/` (árboles vendor/system extraídos con `scripts/extract-android-bits.sh`)
  - `dump/android/mali/` (`libGLES_mali.so`, `vulkan.ums9620.so`, `gralloc.default.so`)
  - `dump/boot/` (`boot_a.img`, kernel y ramdisk extraídos)
  - `dump/live/dlkm/` (módulos de fábrica del kernel 5.4.233)
  - `out/wifi-fw/` (firmware Wi-Fi/BT Unisoc Marlin3)

### Secuencia de compilación paso a paso

1. **Compilar el rootfs base Debian 13:**
   ```bash
   ./scripts/build-rootfs.sh --no-image
   ```
   Genera el árbol Debian mínimo en `out/rootfs`.

2. **Compilar libhybris (capa de compatibilidad Android):**
   ```bash
   ./scripts/build-libhybris.sh
   ./scripts/build-libhybris-glvnd.sh
   ```
   Compila los controladores estándar y vendor GLVND (`libEGL_libhybris.so`, `libGLESv2_libhybris.so`, `eglplatform_drm.so`) en `out/hybris-root` y `out/hybris-glvnd-root`.

3. **Compilar Mesa PanVK (driver Vulkan 1.3 nativo):**
   ```bash
   ./scripts/build-panvk.sh
   ```
   Compila Mesa 26.3 PanVK con soporte Valhall v9 JM (`mali_kbase`) y `VK_KHR_display` dentro del contenedor Docker, generando `out/panvk-dist/libvulkan_panfrost.so`.

4. **Compilar SDL2 con soporte hybris DRM:**
   ```bash
   ./scripts/build-sdl2-hybris.sh
   ```
   Compila SDL2 2.32.4 parcheado con el backend `hybris` en `out/sdl2-hybris-root`.

5. **Compilar EmulationStation Desktop Edition (ES-DE):**
   ```bash
   ./scripts/build-esde.sh
   ```
   Compila ES-DE 3.5.0 con soporte GLES 3.0 enlazado contra SDL2-hybris, instalando en `out/esde-root/opt/es-de`.

6. **Compilar RetroArch y núcleos Libretro:**
   ```bash
   ./scripts/build-retroarch.sh
   ```
   Compila RetroArch 1.22.2 con Vulkan nativo, contexto hybris y puente de vibración física en `out/retroarch-root/opt/retroarch/bin/retroarch`. Los núcleos libretro adicionales se ubican en `out/extra-cores/` y sus archivos info en `out/extra-cores-info/`.

7. **Ensamblar la imagen final de Rootfs (por dispositivo):**
   ```bash
   # Para RG Slide (por defecto)
   DEVICE=rgslide ./scripts/build-full-rootfs.sh out/rootfs-rgslide-v0.9.1.ext4

   # Para RG406H
   DEVICE=rg406h ./scripts/build-full-rootfs.sh out/rootfs-rg406h-v0.9.1.ext4

   # Para RG476H
   DEVICE=rg476h ./scripts/build-full-rootfs.sh out/rootfs-rg476h-v0.9.1.ext4
   ```
   Integra todos los componentes, overlays específicos (`devices/<dispositivo>/overlay/`), firmware, drivers Mali, servicios systemd (`rgslide-sleep`, `rgslide-ch`, `rgslide-imu`, `rgslide-dp`, etc.), configuraciones de ES-DE y produce el sistema ext4 final arrancable.

8. **Empaquetar imagen de arranque (boot por dispositivo):**
   ```bash
   DEVICE=<dispositivo> ./scripts/pack-boot.sh out/boot-<dispositivo>.img
   ```

9. **Flashear al Slot A de pruebas por Fastboot:**
   ```bash
   fastboot flash boot_a out/boot-<dispositivo>.img
   fastboot flash userdata out/rootfs-<dispositivo>-v0.9.1.ext4
   fastboot reboot
   ```

---

## Licencias, oferta de fuentes GPL y transparencia sobre blobs propietarios

- **Código original:** Licencia **MIT** — ver [`LICENSE`](LICENSE).
- **Atribuciones de terceros y oferta de código fuente GPLv3:** Ver [`NOTICE`](NOTICE) y [`THIRD-PARTY.md`](THIRD-PARTY.md) (incluye los commits exactos, parches locales y enlaces a los tarballs de fuentes de **RetroArch 1.22.2**, núcleos **libretro**, **ES-DE 3.5.0**, **libhybris**, **SDL2** y herramientas de AOSP).
- **Manifiesto de hashes y versiones:** Ver [`MANIFEST.md`](MANIFEST.md).
- **Aviso sobre blobs propietarios:**
  - Este **repositorio Git** **no** contiene blobs propietarios ni firmware (`dump/` y `out/` están en `.gitignore`).
  - Los **paquetes de release precompilados** (`rgslide-linux-brom-*.tar.zst`) **SÍ redistribuyen** binarios propietarios extraídos del firmware de fábrica de la RG Slide dentro de `userdata.ext4` (`/opt/android` con `libGLES_mali.so` de ~43 MB, bibliotecas vendor/bionic, módulos del kernel 5.4.233, firmware Wi-Fi/BT) así como `boot.img` y `fdl1`/`fdl2`. Su redistribución es no oficial, únicamente para uso personal e interoperabilidad en hardware RG Slide propiedad del usuario, sin licencia explícita de Arm, Unisoc ni Anbernic.
