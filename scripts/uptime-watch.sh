#!/bin/bash
# Mantiene 10.55.0.1/24 en la interfaz del gadget (NetworkManager la quita) y registra el uptime de la Slide por telnet.
# Uso: scripts/uptime-watch.sh [segundos]  -> out/uptime-watch.log
END=$(( $(date +%s) + ${1:-600} )); LOG=out/uptime-watch.log; : > $LOG
nmcli dev set "$(ip -br link | awk '/^enx/{print $1}' | head -1)" managed no 2>/dev/null
while [ "$(date +%s)" -lt "$END" ]; do
  IF=$(ip -br link | awk '/^enx/{print $1}' | head -1)
  if [ -n "$IF" ]; then
    ip -4 addr show "$IF" | grep -q 10.55.0.1 || { sudo -n ip addr add 10.55.0.1/24 dev "$IF" 2>/dev/null; sudo -n ip link set "$IF" up; }
    U=$(timeout 5 sh -c 'printf "cat /proc/uptime\n" | nc -w 3 10.55.0.2 23 2>/dev/null' | tr -d '\r' | grep -a -E '^[0-9]+\.[0-9]+ ' | tail -1 | cut -d' ' -f1)
    echo "$(date +%T) ${U:-sin-respuesta}" >> $LOG
  else echo "$(date +%T) sin-interfaz" >> $LOG; fi
  sleep 3
done
