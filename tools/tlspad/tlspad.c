/* libtlspad: reserva el INICIO del bloque TLS estatico de cada proceso (tp+0x10 ..) para que ninguna variable __thread real caiga en
 * los slots de TLS de bionic que el blob de la Mali lee/escribe directamente en [tp+0x00..0x70] (p. ej. el contexto GL en [tp+0x18]).
 * Debe cargarse PRIMERO (LD_PRELOAD / /etc/ld.so.preload): las bibliotecas precargadas reciben los primeros offsets de TLS estatico. */
__thread char rgslide_tls_pad[512] __attribute__((aligned(16), used));
