# Sonidos de navegacion de EmulationStation (2026-11-26)

## Por que no suenan
Dos causas encadenadas, las dos en ES, no en el driver:
1. **El mezclador**: `VolumeControl::init()` (es-app/src/VolumeControl.cpp) hace `snd_mixer_find_selem("Master")` sobre la tarjeta `default`. El codec de la Slide no expone ningun control simple llamado `Master`, asi que `mixerElem` queda nulo y el primer paso de `VolumeControl::setVolume` (bajar/`setVolume`) hace `LOG(LogError) << ... << mixerElem->getVolume()` con el puntero nulo. `es_settings.cfg` trae `AudioCard=default`, `AudioDevice=Master`.
2. **El estado de reproduccion**: `AudioManager::init()` abre el PCM una vez y lo deja **pausado** (`SDL_OpenAudio` sin callback activo). Solo se despausa en `AudioManager::play()`, y `Sound::play()` sale antes de llegar ahi si no hay muestra cargada (y antes de todo si `EnableSounds=false`). Con `EnableSounds=true` por defecto, el camino de cada sonido acaba en el mezclador.

## Que se cambia (reversible)
- `etc/emulationstation/settings-overrides.cfg`: `EnableSounds=false`. ES deja de usar SDL/ALSA para el menu y no toca el PCM, con lo que **no hay segundo cliente** que pueda abrir el PCM real del altavoz mientras `rgslide-audiod` lo tiene; los juegos siguen igual (RetroArch ya abre y cierra su propio PCM antes/despues de la pantalla).
- El sonido de navegacion lo hace el demonio del mando (`rgslide-hotkeyd`) escribiendo en el PCM **`rgslide`** de `/etc/asound.conf` (alias del FIFO, distinto de `default` para no pisar el canal del propio audiod, ver el comentario del fichero).
- Piezas: `tools/es-sounds/` (`nav.wav` generado, sin licencia de terceros) y el helper `usr/local/bin/rgslide-es-navsound` (aplay al PCM `rgslide`, con cerrojo para no solapar).

## Despliegue y vuelta atras
- `scripts/install-es-sounds.sh install` (o `undo`); reiniciar ES para que lea el ajuste.
- Vuelta atras fina sin reinstalar: `EnableSounds` a `true` en `~/.emulationstation/es_settings.cfg` (el demonio sigue sonando igual, solo se suman los de ES).
- `undo` ademas quita `/etc/emulationstation/settings-overrides.cfg`, el helper y el WAV.

## Pendiente de verificar en la Slide
- Que `aplay` exista (en la comprobacion del 26-11 `aplay -l` dio *command not found*): si no esta, el helper cae a escribir el WAV crudo en `/run/rgslide/audio.fifo` (S16_LE, 2 canales, 48000 Hz) y hay que resamplear.
- Ajustar los eventos del demonio (arriba/abajo/aceptar/atras) a los que de verdad mueven el menu; y subir el volumen del WAV si queda bajo.
- Con `ALWAYS_ON=0` hay latencia de apertura del PCM en el primer sonido tras el silencio (ver `/etc/rgslide/audio.conf`); con `ALWAYS_ON=1` el sonido es inmediato.
- No probado en la Slide: el 26-11 quedo sin SSH tras el primer `s2idle` (docs/agent-sleep.md).
