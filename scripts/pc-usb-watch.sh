#!/bin/bash
# Lado PC de la prueba del gadget RNDIS: registra eventos USB, pone 10.55.0.1/24 cada vez que aparece la interfaz,
# prueba ping y telnet, y captura trafico. Todo en out/usbwatch-<hora>/. Uso: scripts/pc-usb-watch.sh [segundos]
WAIT=${1:-420}
D=out/usbwatch-$(date +%H%M%S); mkdir -p "$D"
echo "registro en $D"
sudo -n dmesg -w --time-format iso 2>/dev/null | grep --line-buffered -E 'usb 1-4|rndis|cdc_|enx|usb0' > "$D/dmesg.log" &
P1=$!
trap 'kill $P1 $P2 2>/dev/null' EXIT
last=""; end=$(( $(date +%s) + WAIT )); got=0
while [ "$(date +%s)" -lt "$end" ]; do
  IF=$(for d in /sys/bus/usb/devices/*; do [ "$(cat $d/idProduct 2>/dev/null)" = 0104 ] && [ "$(cat $d/idVendor 2>/dev/null)" = 1d6b ] && ls $d/*/net 2>/dev/null; done | head -1)
  if [ -n "$IF" ]; then
    if [ "$IF" != "$last" ]; then
      echo "$(date +%T) interfaz $IF apareció"; last=$IF
      sudo -n ip link set "$IF" up; sudo -n ip addr add 10.55.0.1/24 dev "$IF" 2>/dev/null
      [ -n "$P2" ] && kill $P2 2>/dev/null
      sudo -n tcpdump -i "$IF" -n -w "$D/cap-$IF.pcap" >/dev/null 2>&1 & P2=$!
    fi
    for t in /dev/ttyUSB*; do
      [ -c "$t" ] || continue
      grep -q "$t" "$D/tty.done" 2>/dev/null && continue
      echo "$t" >> "$D/tty.done"
      echo "$(date +%T) puerto serie $t: pruebo la shell"
      sudo -n stty -F "$t" 115200 raw -echo 2>/dev/null
      sudo -n timeout 8 sh -c "exec 3<>$t; printf '\n\necho MARCA-TTY-\$(uname -r); touch /run/keep; ls /sys/class/udc; cat /sys/class/udc/*/state\n' >&3; sleep 3; dd bs=4096 count=1 <&3 2>/dev/null" > "$D/tty-$(basename $t).txt" 2>&1
      echo "$(date +%T) respuesta tty: $(wc -c < "$D/tty-$(basename $t).txt") bytes"; head -c 400 "$D/tty-$(basename $t).txt"; echo
    done
    if ping -c1 -W1 10.55.0.2 >/dev/null 2>&1; then
      echo "$(date +%T) PING OK a 10.55.0.2"; got=1
      timeout 8 sh -c 'printf "uname -a; cat /proc/cmdline | cut -c1-120; ls /sys/class/udc; cat /sys/class/udc/*/state; touch /run/keep; echo FIN\n" | nc -w 6 10.55.0.2 23' > "$D/telnet.txt" 2>&1
      echo "$(date +%T) telnet: $(wc -c < "$D/telnet.txt") bytes"; [ -s "$D/telnet.txt" ] && head -c 600 "$D/telnet.txt"
      break
    fi
  elif [ -n "$last" ]; then echo "$(date +%T) interfaz $last DESAPARECIO"; last=""; fi
  sleep 1
done
[ $got = 0 ] && echo "$(date +%T) sin respuesta de 10.55.0.2 en ${WAIT}s"
