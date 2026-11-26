# Refresco > 60 Hz en la RG Slide (2026-11-25)

**Resultado: 120 Hz SÍ funciona** (medido con pantalla real). Por defecto sigue en 60 Hz; se activa con `HYBRIS_DRM_REFRESH=120`.

## 1. Modos que ofrece el kernel (MEDIDO, GETCONNECTOR sobre DSI-1, conector 178)
```
1280x960 clk 182000 h 1280 1294 1304 1314  v 960 1981 1991 2305  vrefresh  60  type 0x48 (preferido|driver)
1280x960 clk 182000 h 1280 1294 1304 1314  v 960 1094 1104 1154  vrefresh 120  type 0x40
1280x960 clk 182000 h 1280 1294 1304 1314  v 960 1475 1485 1535  vrefresh  90  type 0x40
```
`/sys/class/drm/card0-DSI-1/modes` imprime tres líneas `1280x960` (sin Hz); por eso parecía un solo modo. DP-1 desconectado (0 modos).
Mismo pixel clock (182 MHz) y mismo horizontal: solo cambia el porche vertical (vtotal 2305/1535/1154).

## 2. Evidencia de que el panel lo soporta
- DT del panel (`/proc/device-tree/lcds/lcd_ft8201p_ylm_lbf047001h_mipi_1280x960/display-timings`): `timing0/1/2` con vfront-porch 1021/515/134 y vback-porch 314/50/50 (= los tres modos), `native-mode` = phandle de timing0 (60 Hz); `sprd,vrr-enabled`, `sprd,vrr-max-layers=2`, `sprd,dpi-clk-div=6`.
- GammaOS (`dump/live/dmesg-gamma-now.txt`): `persist.gammaos.refresh.rate=120` (getprop), servicio `setdisplay_120`/`refreshlock`, y a los 15.109 s un `sprd_dpu_mode_set_nofb() mode: ...120 ... 1094 1104 1154` seguido de `dpu is idle now / dpu stop` (el DPU se reprograma).
- Sysfs del DPU: `/sys/class/display/dispc0/actual_fps` (medido real) y `panel0/panel_fps` (calculado del modo *guardado*, se queda en 60.9: no es fiable para esto). `dispc0/refresh` es escritura-solo (`refresh_store`), no usado.

## 3. Causa de que SETCRTC no bastara (MEDIDO + desensamblado de `sprd-drm.ko`)
- Elegir el modo de 120 Hz con SETCRTC legacy (lo que hacía la primera versión del parche): el kernel loguea `sprd_dpu_mode_set_nofb() ... 120` pero **el panel sigue a 60** (`actual_fps` 60, flips cada 16,6 ms). Probado también a 90.
- `dpu_modeset` (sprd-drm.ko) solo marca el cambio de frecuencia si el estado del CRTC trae una de las propiedades CRTC `"resolution change"` o **`"frame rate change"`** (bytes 504/505 del estado; se ven con `sprd_crtc_atomic_set_property`/`sprd_crtc_init`). Sin ella `dpu_flip` no llama a `sprd_dsi_vrr_timing` y los registros de porche no se reescriben. SETCRTC legacy no puede poner propiedades.
- Con un commit **atómico** (`CRTC_ID`+`MODE_ID`+`ACTIVE`+`frame rate change=1`, `ALLOW_MODESET`) funciona sin parar el DPU a mano.

## 4. Mediciones (hybris/EGL real, `scripts/refresh-test.sh`, 6 s, ES parado ~8 s por prueba)
| | 60 Hz | 120 Hz |
|---|---|---|
| `hybristest` (GLES 3.2, Mali-G57, 1280x960) | 59,0 fps | **108,4 fps** |
| `actual_fps` (dispc0) | 60,1-60,7 | **119,1-121,9** |
| Corriente de batería (`current_now`, media de 6 s) | 252 mA | 287 mA (**+35 mA, ~+14 %**) |
| Intervalo de flip real (`refresh-flip`) | 16,6 ms | 8,8 ms (113,9 Hz medido en 3 s) |
- GPU devfreq 384 MHz en ambos (no sube sola). `hybristest` no llega a 120 por coste propio de la escena, no por el panel.
- Cifra de consumo orientativa: la batería está al 100 % ("Full", cargador conectado por USB) y `current_now` es el del fuel-gauge; no es una medida de potencia de pared.
- 90 Hz por el mismo camino dio 68,5 Hz en el flip, `actual_fps` 50 (inestable): **no usar 90 Hz**, solo 60 y 120.

## 5. Qué queda hecho
- `patches/libhybris/0004-drm-modo-por-HYBRIS_DRM_REFRESH.patch` (sobre 0002; comprobado: `git apply` limpio 0002+0003+0004 sobre upstream y el árbol `third_party/libhybris` queda idéntico). Con la variable ausente o `=60`: **mismo comportamiento que antes** (medido: `panel 1280x960@60`, 59 fps). Con `=120`: tras el primer SETCRTC a 60 Hz hace el commit atómico; si falla, sigue a 60 y lo dice por stderr. Al destruir la ventana vuelve a 60 Hz por atómico (`hybris drm: modo 120 Hz` en el log al activarse). Si el valor pedido no existe, cae a 60.
- Compilado (`make` en `out/hybris-build/egl/platforms/drm`): `out/hybris-build/egl/platforms/drm/.libs/eglplatform_drm.so`. **NO instalado** en `/opt/hybris*` (lo vivo sigue siendo la versión anterior: md5 f6bd58e4...). Para pruebas está en la Slide en `/tmp/hyb120/eglplatform_drm.so` (se pierde al reiniciar).
- `scripts/refresh-test.sh HZ [s] [dir]` (en la Slide: para ES, `hybristest` con `timeout 12`, ES se arranca SIEMPRE por `trap`; al volver ES hace su SETCRTC a 60 Hz). `scripts/refresh-flip.c` (+ `scripts/refresh-flip-test.sh`): fija modo por SETCRTC o atómico y mide el intervalo de flip; `-static`, `aarch64-linux-gnu-gcc -O2 -static -o out/refresh-flip scripts/refresh-flip.c`.
- Probado por las dos rutas, 0 panics/oops en dmesg, `rgslide-es` `active` tras cada prueba.

## 6. Activar / deshacer
- Activar para una prueba: `HYBRIS_DRM_REFRESH=120 HYBRIS_EGLPLATFORM_DIR=/tmp/hyb120 rgslide-hybris-env /opt/hybris/bin/hybristest 5` (con ES parado) o con `scripts/refresh-test.sh 120 6 /tmp/hyb120`.
- Instalar (requiere OK): copiar el `.so` a `/opt/hybris/lib/libhybris/` y `/opt/hybris-glvnd/aux/libhybris/` (guardar antes el actual). Para que ES use 120 Hz: `Environment=HYBRIS_DRM_REFRESH=120` en `rgslide-es.service` (no hecho). RetroArch (juegos a 60 fps) con el panel a 120: sin probar; `video_refresh_rate` de RetroArch debería ajustarse o el sincronismo cambiará.
- Deshacer: quitar la variable (o `=60`); reiniciar ES vuelve a 60 Hz aunque algo quede en 120.
- Riesgo conocido: el `.so` nuevo mantiene en 60 Hz el arranque salvo variable; si el commit atómico falla solo emite un aviso.

## 7. Lo que debe ver el usuario con los ojos (SIN verificar por persona)
- Que con `HYBRIS_DRM_REFRESH=120` el desplazamiento/animación (p. ej. scroll de ES) se vea más fluido y sin parpadeo, brillo igual que a 60 Hz (el PWM/gamma del panel no se ha tocado).
- Que al cerrar el programa la pantalla vuelva a 60 Hz sin parpadeo ni franjas, y que ES arranque a la primera.
- Los tests del agente solo midieron `actual_fps` y flips; nadie ha mirado la pantalla a 120 Hz.
