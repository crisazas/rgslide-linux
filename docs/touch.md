# Prueba del táctil en la RG Slide

Herramienta propia, pensada para que la maneje una persona: `tools/touchtest/touchtest.c` y `scripts/run-touchtest.sh`.

- `scripts/run-touchtest.sh [segundos]` compila en cruzado, instala `rgslide-touchtest`, **para `rgslide-es`**, ejecuta la prueba a pantalla completa y **siempre vuelve a arrancar ES** (un solo proceso con la pantalla, regla del proyecto).
- Tres fases: tocar 9 puntos (mide el error de cada uno), seguir una línea en zigzag de punta a punta (recorrido, desvío y muestras/s) y dibujar con varios dedos (contactos simultáneos). Start+Select sale; botón A salta de fase.
- Lee `fts_ts` por evdev directamente (protocolo MT tipo B), no depende de `SDL_FINGER*`.
- Resultado en pantalla y en `/tmp/touchtest.log` de la Slide.

## Medido (2026-11-26)

| Prueba | Resultado |
|---|---|
| Eventos del panel | 570 eventos MT de movimiento, 117 muestras/s siguiendo la línea |
| 9 objetivos | 9/9 tocados (3 con un primer toque fuera), error medio 32-38 px |
| Línea completa | recorrido 99-100 % en 2,9 s; desvío medio 20-22 px, máx 48-53 px |
| Varios dedos | 4 contactos simultáneos |
| SDL | 13 FINGERDOWN / 13 FINGERUP: el panel llega también a SDL |

- Los errores de 20-40 px son de puntería del dedo, no del panel (el toque que cae cerca del objetivo se corrige al apoyar).
- Descentrado por zona: esquina superior izquierda dx -40 px, arriba-centro dx +66 px; el resto entre -13 y +45 px. Orientativo: un desplazamiento de 40-60 px en 1280x960 no impide usar la interfaz.
