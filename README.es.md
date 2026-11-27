# rgslide-linux: Linux nativo en la Anbernic RG Slide (Unisoc T820 / ums9620_2h10)

> **Idioma:** [English (README.md)](README.md) | Español
> *(Las notas técnicas en `docs/` y el historial de commits se mantienen en español.)*

> **Estado: Pre-release experimental (`v0.9.x`) — NO es una versión `1.0` estable.**
> La estabilidad de arranque aún requiere validación (una condición de carrera entre los módulos de fábrica `apsys_dvfs` y `mmdvfs` causó 2 panics en 14 arranques medidos; existe un `softdep` mitigador con verificación en frío aún limitada), la suspensión profunda (`s2idle`) está restringida y la salida DisplayPort por USB-C aún no funciona.

Distribución Linux nativa (Debian 13 Trixie arm64) para la consola portátil **Anbernic RG Slide**, reemplazando Android por completo con aceleración 3D por hardware (GPU Mali-G57 vía `libhybris`), EmulationStation Desktop Edition (ES-DE), RetroArch, audio, Wi-Fi, Bluetooth y controles físicos completos.

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
  - Botones físicos de volumen (`/dev/input/event0`) integrados con `rgslide-hotkeyd` y `rgslide-volume`.
  - Sensor Hall del deslizador (`KEY_WAKEUP`) y pulsación corta del botón Power activan reposo instantáneo (`rgslide-sleepd`): apaga retroiluminación (`brightness=0`), pausa (`SIGSTOP`) ES-DE y RetroArch a mitad de juego y reduce las CPUs a `powersave`; al abrir o volver a pulsar reanuda de inmediato.
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

El script de flasheo está versionado y auditable en [`tools/flash-rgslide.sh`](tools/flash-rgslide.sh):

1. Descomprime el archivo `.tar.zst` en un PC Linux x86_64 y verifica `SHA256SUMS`.
2. Previsualiza exactamente qué se va a escribir sin tocar la consola:
   ```bash
   ./flash-rgslide.sh --dry-run
   ```
3. Apaga la RG Slide por completo (`Vol-Down + Power` si estuviera colgada).
4. Mantén pulsado el botón **Back** (lado derecho de la pantalla) y conecta el cable USB-C al PC.
5. Ejecuta el flasher (escribe `boot_b`, `misc` y `userdata`, pidiendo confirmación `FLASH`):
   ```bash
   sudo ./flash-rgslide.sh
   ```

### Iterar en desarrollo (Fastboot -> Slot A)

```bash
./tools/flash-rgslide.sh --mode fastboot --slot a --dir out --dry-run
./tools/flash-rgslide.sh --mode fastboot --slot a --dir out
```

---

## Licencias, oferta de fuentes GPL y transparencia sobre blobs propietarios

- **Código original:** Licencia **MIT** — ver [`LICENSE`](LICENSE).
- **Atribuciones de terceros y oferta de código fuente GPLv3:** Ver [`NOTICE`](NOTICE) y [`THIRD-PARTY.md`](THIRD-PARTY.md) (incluye los commits exactos, parches locales y enlaces a los tarballs de fuentes de **RetroArch 1.22.2**, núcleos **libretro**, **ES-DE 3.5.0**, **libhybris**, **SDL2** y herramientas de AOSP).
- **Manifiesto de hashes y versiones:** Ver [`MANIFEST.md`](MANIFEST.md).
- **Aviso sobre blobs propietarios:**
  - Este **repositorio Git** **no** contiene blobs propietarios ni firmware (`dump/` y `out/` están en `.gitignore`).
  - Los **paquetes de release precompilados** (`rgslide-linux-brom-*.tar.zst`) **SÍ redistribuyen** binarios propietarios extraídos del firmware de fábrica de la RG Slide dentro de `userdata.ext4` (`/opt/android` con `libGLES_mali.so` de ~43 MB, bibliotecas vendor/bionic, módulos del kernel 5.4.233, firmware Wi-Fi/BT) así como `boot.img` y `fdl1`/`fdl2`. Su redistribución es no oficial, únicamente para uso personal e interoperabilidad en hardware RG Slide propiedad del usuario, sin licencia explícita de Arm, Unisoc ni Anbernic.
