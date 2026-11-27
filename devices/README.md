# Perfiles de Dispositivos (Device Profiles)

Esta carpeta contiene la configuración específica por modelo para permitir soporte multi-dispositivo sobre la misma plataforma base Unisoc T820 (`ums9620_2h10`), sin modificar los scripts ni la configuración base de la RG Slide.

## Dispositivos soportados:
- `rgslide`: Anbernic RG Slide (panel 1280x960 120Hz con slider mecánico y sensor Hall).
- `rg406h`: Anbernic RG406H (panel 960x720 60Hz horizontal con sticks Hall e iluminación RGB).

## Convención:
Cada dispositivo define:
- `config.env`: Variables de entorno para resolución, tasa de refresco, sensores y periféricos.
- `overlay/`: Sobrescrituras o servicios específicos que solo aplican a este modelo.
- `udev/`: Reglas de udev para joysticks y permisos si difieren del modelo base.
