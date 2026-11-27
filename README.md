# rgslide: Linux en la Anbernic RG Slide (Unisoc T820 / ums9620_2h10)

Distribución Linux nativa (Debian 13 Trixie arm64) para la consola portátil **Anbernic RG Slide**, reemplazando completamente Android con aceleración gráfica por hardware (GPU Mali-G57), interfaz EmulationStation Desktop Edition (ES-DE), audio, Wi-Fi, Bluetooth y controles físicos integrados.

---

## 🎮 Estado del Proyecto: ¿Qué funciona y qué está pendiente?

### ✅ Lo que funciona (Totalmente funcional)

* **Pantalla y Gráficos 3D por Hardware (GPU Mali-G57):**
  - Panel nativo a resolución completa con rotación correcta y tasa de refresco fluida (hasta 120 Hz).
  - Aceleración OpenGL ES 2.0 / 3.x mediante `libhybris` + wrapper vendor Mali + parches de TLS (`tlspad`).
  - Frontend **EmulationStation Desktop Edition (ES-DE)** y emuladores **RetroArch** corriendo nativos con aceleración GPU OpenGL.
* **Audio Completo:**
  - Salida de audio estéreo por altavoces internos y jack de auriculares mediante codec Unisoc UMP9620 (`snd_soc_sprd_codec_ump9620`).
  - Resuelto orden de inicialización de reguladores de audio (`SRG_VB`, `SRG_BIAS`) evitando ruido/estática estática y ruidos de pop (`DPOP`).
  - Demonio de audio `rgslide-audiod` con soporte ALSA para mezcla y sonido de navegación en menús de ES-DE y emuladores.
* **Controles y Botones Físicos:**
  - Gamepad interno (`retrogame_joypad`, `/dev/input/event3`) completamente mapeado y calibrado.
  - Botones físicos de volumen (rocker lateral `/dev/input/event0`) integrados con `rgslide-hotkeyd` y `rgslide-volume`.
  - Menú de atajos en pantalla / overlay de herramientas (`rgslide-ui`).
* **Conectividad:**
  - **Wi-Fi:** Conexión estable a redes 2.4/5 GHz con asignación automática de IP por DHCP (`systemd-networkd`).
  - **Bluetooth:** Inicialización de hardware Bluetooth (`rgslide-bt-init`), demonio `bluetoothd` activo y emparejamiento de periféricos/auriculares (`rgslide-bt-pair`).
  - **USB:** Red USB Gadget (`rndis` / `cdc_ether`) para depuración y acceso por SSH (`console@10.55.0.2`).
* **Sistema y Gestión:**
  - Base Debian 13 (Trixie) arm64 minimal.
  - Coprocesador CM4 activo y alimentado (`rgslide-cm4-prep`).
  - Sincronización dinámica de idioma: el menú de configuración "Tools" se adapta automáticamente al idioma configurado en ES-DE (Español / Inglés).

---

### ⏳ Pendiente / En Desarrollo

* **Modo Suspensión Profunda (Deep Sleep / S2idle):**
  - Actualmente limitado a suspensión ligera para evitar bloqueos del bus I2C/sensorhub al reanudar.
* **Salida de Vídeo Externa (USB-C DisplayPort / HDMI Alt Mode):**
  - Pendiente de mapeo de negociación DP y conmutación de salida de vídeo.
* **Cámara frontal:**
  - No requerida para emulación, pero los drivers de Unisoc ISP no están cargados en el espacio de usuario.
* **Mezcla ALSA directa multi-cliente:**
  - Actualmente pasa por el pipeline de `rgslide-audiod`.

---

## 🛠️ Arquitectura del Sistema

```
+-------------------------------------------------------------+
|              EmulationStation Desktop Edition               |
|                         RetroArch                           |
+-------------------------------------------------------------+
|     SDL2-hybris (con ALSA audio)    |     EGL / GLES v2     |
+-------------------------------------+-----------------------+
|  libhybris (linker & hooks) + tlspad (TLS align 64B)        |
+-------------------------------------------------------------+
|          Android Vendor Blobs (libGLES_mali.so, etc.)       |
+-------------------------------------------------------------+
|    Linux Kernel 5.4.233 (Stock Anbernic / Unisoc T820)      |
+-------------------------------------------------------------+
|             Hardware Anbernic RG Slide (ums9620)            |
+-------------------------------------------------------------+
```

---

## 📂 Estructura del Repositorio

- `rootfs/overlay/`: Archivos de configuración del sistema Debian (`/etc`, `/usr/local/bin`, `/usr/local/sbin`, servicios systemd).
- `tools/`: Herramientas propias (`rgslide-ui`, `rgslide-ctl`, `tlspad`, etc.).
- `patches/`: Parches aplicados a componentes (`libhybris`, kernel, etc.).
- `scripts/`: Scripts de preparación de imágenes, particionado y empaquetado.
- `docs/`: Documentación técnica de ingeniería inversa y bring-up del hardware.

---

## ⚠️ Advertencia y Descargo de Responsabilidad

Este proyecto reemplaza el sistema operativo de serie de la consola. Para volver al estado de fábrica de Android se requiere restaurar el volcado original mediante modo BROM (`spd_dump`).
