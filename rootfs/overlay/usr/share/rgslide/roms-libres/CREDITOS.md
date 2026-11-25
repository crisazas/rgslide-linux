# ROMs de prueba de la RG Slide: proyectos libres

Solo juegos cuyo autor publica el binario (la ROM) con licencia que permite compartirlo, con su codigo fuente disponible. Cada fichero es la ROM
SIN MODIFICAR de la version publicada por el autor; se verifica con su `sha256`. Gracias a sus autores: jugarlos y visitar sus paginas es la mejor forma de agradecerselo.

| Fichero | Juego | Sistema | Autor | Licencia | Fuente |
|---|---|---|---|---|---|
| `gb/libbet.gb` | Libbet and the Magic Floor v0.08 | Game Boy | Damian Yerrick (sobre el Magic Floor de Martin Korth) | zlib | https://github.com/pinobatch/libbet |
| `gb/adjustris.gb` | Adjustris v1.1 | Game Boy | Dave VanEe (tbsp) | CC0 1.0 | https://github.com/tbsp/Adjustris |
| `gbc/ucity.gbc` | uCity v1.3 | Game Boy Color | Antonio Nino Diaz (AntonioND) | Codigo GPL-3.0-o-posterior (GBT Player: BSD-2-Clause); graficos y musica CC BY-SA 4.0 | https://github.com/AntonioND/ucity |
| `nes/thwaite.nes` | Thwaite v0.04 | NES | Damian Yerrick | GPL-3.0-o-posterior | https://github.com/pinobatch/thwaite-nes |
| `nes/nova.nes` | Nova the Squirrel v1.0.6a | NES | NovaSquirrel | Codigo GPL-3.0-o-posterior; **recursos (graficos, niveles) CC BY-NC-SA 4.0: solo uso no comercial** | https://github.com/NovaSquirrel/NovaTheSquirrel |
| `gba/glacia.gba` | Glacia Dungeon v1.5.2 | Game Boy Advance | NotImplementedLife (GBAJam 2022) | GPL-3.0 (LICENSE.md del repo; el README solo agradece logo, instrumentos de ModArchive y libgba) | https://github.com/NotImplementedLife/GlaciaDungeon |
| `gba/notebook.gba` | Notebook Adventure v1.3 | Game Boy Advance | NotImplementedLife (Retro Platform Jam #4) | GPL-3.0 (LICENSE del repo) | https://github.com/NotImplementedLife/NotebookAdventure |
| `nds/delve.nds` | Delve v2.5.1 | Nintendo DS | Michal Pandyra (codigo con ayuda de Claude); arte de Shade y devurandom (CC0); musica de Kosmo The Cat (CC0), Beyond, Drozerix y JAM (dominio publico) | Codigo MIT; BlocksDS, libnds, Nitro Engine y Maxmod como bibliotecas (detalle en CREDITS.md/LICENSES.md del repo) | https://github.com/pandymic/delve-nds |

## sha256 completos (de la release del autor; coinciden con los que publica el recopilador slippylabs para libbet, ucity, thwaite y nova)
```
3607412031c8287cf878299ce96e581e85b852dde703806343b95576fa3ff1a9  libbet.gb      (v0.08)
b6c8affe6d906419cfc99ff459718f33a1868af03254a2f65cea2a9430394712  adjustris.gb   (v1.1)
9422ee2ca7b7ea1d46b58b2a429fff3f354dfd3e732dee1e7ae6220f148ce6e0  ucity.gbc      (v1.3)
a2df24d9c9f72e56c2fdc4c703becc47a5700ad0158da8208247635ebeb3779c  thwaite.nes    (v0.04)
e4780e90b9d1587489bfb797d2ca395be21371ea9262fa9f87f99324ec6960ab  nova.nes       (v1.0.6a)
```
(`adjustris.gb` no figura en ese recopilador: su hash es el de mi descarga de la release del autor.)

## sha256 completos de las ROMs GBA y NDS (de la release del autor; sin recopilador independiente con el que contrastar)
```
976418b666b9359e1078c652947aa70ad46e9be53f3e9d64474f1fac5a138749  glacia.gba     (v1.5.2)
944ed052a38f7d01df79bae760f102e5c19527471fc834c8986cb19606fb7e9a  notebook.gba   (v1.3)
4957cf1c4094c66fbf898c1da2fe15a016a7ce2fa84840545340c185ba386815  delve.nds      (v2.5.1)
```

## Descartados a proposito
- **Porklike (Game Boy)**: solo el codigo del port es MIT; "todo el contenido es de los titulares originales" (README de binji/porklike.gb).
- **BeatBeast (GBA)**: MIT, pero 15 KB de licencias de terceros y los recursos no se verificaron; no se redistribuye.

## Nota
Para los juegos con codigo GPL, el codigo fuente completo de cada version exacta esta en el repositorio de la columna "Fuente", en la etiqueta de la release indicada.
- **ds-craft (NDS)**: codigo MIT, pero texturas, fuente y sonidos son de Mojang.
- **Skyjo-DS / Traffic-Escape-DS (NDS)**: son adaptaciones de juegos con marca; no se verifico el origen de los recursos.
- **2048-nds**: "usa recursos del juego de Professor Layton".
