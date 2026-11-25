# drmshow

Muestra un patron de prueba o un PPM (P6) en `/dev/dri/card0` con ioctls DRM directos (sin libdrm): CREATE_DUMB XRGB8888, ADDFB2 (o ADDFB), SETCRTC legacy; si SETCRTC da ENOSYS/EINVAL/EOPNOTSUPP prueba `DRM_IOCTL_MODE_ATOMIC` (plano primario). Restaura el CRTC original al terminar.

**NO probado en la Slide.** Solo compilado en el PC.

## Uso
    drmshow [-d /dev/dri/card0] [-m WxH[@R]] [-t segundos] (-p | fichero.ppm)
- `-p` barras de color + marco blanco de 4 px + degradados; `fichero.ppm` se escala por vecino mas cercano (sin rotar).
- `-t N` segundos mostrado (por defecto 10); Ctrl-C tambien restaura.
- Sin `-m` elige el modo preferido de mayor tasa, o el primero.
- Salida 3 = el driver no soporta CREATE_DUMB (se imprime el errno).

## Compilar y probar
    scripts/build-drmshow.sh
    scp out/drmshow console@10.55.0.2:/tmp/
    ssh console@10.55.0.2 'sudo /tmp/drmshow -p -t 10'
Requiere que exista `/dev/dri/card0` (tras `scripts/gpu-prep.sh`).
