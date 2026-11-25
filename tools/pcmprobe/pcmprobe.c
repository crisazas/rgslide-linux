/* pcmprobe: consulta los limites de un PCM ALSA con ioctls crudos, SIN reservar ni escribir el buffer.
 * Uso: pcmprobe /dev/snd/pcmC0D2p            -> solo HW_REFINE (seguro, no reserva memoria)
 *      pcmprobe /dev/snd/pcmC0D2p -w BYTES   -> ademas HW_PARAMS con buffer de BYTES (PUEDE provocar panic en sprd-pcm, ver docs/audio-wifi.md) */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sound/asound.h>

#define IMASK(p, n) (&(p)->masks[(n) - SNDRV_PCM_HW_PARAM_FIRST_MASK])
#define IVAL(p, n) (&(p)->intervals[(n) - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL])

static void full(struct snd_pcm_hw_params *p)
{
	memset(p, 0, sizeof *p);
	for (int i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_MASK - SNDRV_PCM_HW_PARAM_FIRST_MASK; i++)
		memset(&p->masks[i], 0xff, sizeof p->masks[i]);
	for (int i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; i++) {
		p->intervals[i].min = 0; p->intervals[i].max = ~0U;
	}
	p->rmask = ~0U;
	p->cmask = 0; p->info = ~0U;
}

static void setmask(struct snd_pcm_hw_params *p, int n, unsigned bit)
{
	struct snd_mask *m = IMASK(p, n);
	memset(m, 0, sizeof *m); m->bits[bit >> 5] = 1U << (bit & 31);
}
static void setiv(struct snd_pcm_hw_params *p, int n, unsigned v)
{
	struct snd_interval *i = IVAL(p, n);
	i->min = i->max = v; i->integer = 1; i->openmin = i->openmax = 0;
}
static void show(struct snd_pcm_hw_params *p)
{
	static const struct { int n; const char *s; } L[] = {
		{ SNDRV_PCM_HW_PARAM_CHANNELS, "channels" }, { SNDRV_PCM_HW_PARAM_RATE, "rate" },
		{ SNDRV_PCM_HW_PARAM_PERIOD_BYTES, "period_bytes" }, { SNDRV_PCM_HW_PARAM_PERIODS, "periods" },
		{ SNDRV_PCM_HW_PARAM_BUFFER_BYTES, "buffer_bytes" }, { SNDRV_PCM_HW_PARAM_BUFFER_SIZE, "buffer_size(frames)" },
		{ SNDRV_PCM_HW_PARAM_PERIOD_SIZE, "period_size(frames)" }, { SNDRV_PCM_HW_PARAM_FRAME_BITS, "frame_bits" } };
	for (unsigned i = 0; i < sizeof L / sizeof *L; i++) { struct snd_interval *v = IVAL(p, L[i].n); printf("  %-22s %u .. %u\n", L[i].s, v->min, v->max); }
	printf("  format mask  %08x  access mask %08x\n", IMASK(p, SNDRV_PCM_HW_PARAM_FORMAT)->bits[0], IMASK(p, SNDRV_PCM_HW_PARAM_ACCESS)->bits[0]);
	fflush(stdout);
}

int main(int argc, char **argv)
{
	if (argc < 2) return 2;
	int fd = open(argv[1], O_RDWR);
	if (fd < 0) { perror("open"); return 1; }
	struct snd_pcm_hw_params p;
	full(&p);
	if (ioctl(fd, SNDRV_PCM_IOCTL_HW_REFINE, &p) < 0) { perror("HW_REFINE"); return 1; }
	printf("HW_REFINE ok (sin reservar buffer):\n"); show(&p);
	if (argc >= 2 && argc < 4 + 0 && argc == 3 && !strcmp(argv[2], "-s")) {
		/* Solo HW_REFINE: que period_bytes / periods acepta el driver con S16_LE, 2 canales, 48 kHz. */
		static const unsigned pb[] = { 320, 1024, 4096, 8192, 16384, 32768, 38400, 48000, 65536, 116720 };
		for (unsigned i = 0; i < sizeof pb / sizeof *pb; i++) {
			for (int np = 0; np < 2; np++) {
				struct snd_pcm_hw_params q; full(&q);
				setmask(&q, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
				setmask(&q, SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FORMAT_S16_LE);
				setiv(&q, SNDRV_PCM_HW_PARAM_CHANNELS, 2);
				setiv(&q, SNDRV_PCM_HW_PARAM_RATE, 48000);
				setiv(&q, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, pb[i]);
				if (np) setiv(&q, SNDRV_PCM_HW_PARAM_PERIODS, 2);
				int r = ioctl(fd, SNDRV_PCM_IOCTL_HW_REFINE, &q);
				struct snd_interval *b = IVAL(&q, SNDRV_PCM_HW_PARAM_BUFFER_BYTES), *n = IVAL(&q, SNDRV_PCM_HW_PARAM_PERIODS);
				printf("period_bytes=%-6u periods=%s -> %s  periods %u..%u buffer_bytes %u..%u\n", pb[i], np ? "2" : "libre", r < 0 ? "EINVAL" : "ok", n->min, n->max, b->min, b->max);
			}
		}
		return 0;
	}
	if (argc >= 4 && !strcmp(argv[2], "-w")) {
		unsigned bytes = atoi(argv[3]);
		struct snd_pcm_hw_params q; full(&q);
		setmask(&q, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
		setmask(&q, SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FORMAT_S16_LE);
		setiv(&q, SNDRV_PCM_HW_PARAM_CHANNELS, 2);
		setiv(&q, SNDRV_PCM_HW_PARAM_RATE, 48000);
		setiv(&q, SNDRV_PCM_HW_PARAM_PERIODS, 2);
		setiv(&q, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, bytes / 2);
		/* Partir de lo que HW_REFINE deja tras fijar formato/canales/tasa: el driver rellena el resto y valida la combinacion. */
		if (ioctl(fd, SNDRV_PCM_IOCTL_HW_REFINE, &q) < 0) { perror("HW_REFINE (con restricciones)"); show(&q); return 1; }
		printf("tras fijar S16/2ch/48k:\n"); show(&q);
		setiv(&q, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, bytes / 2);
		setiv(&q, SNDRV_PCM_HW_PARAM_PERIODS, 2);
		q.rmask = ~0U;
		if (ioctl(fd, SNDRV_PCM_IOCTL_HW_REFINE, &q) < 0) { perror("HW_REFINE (buffer pedido)"); show(&q); return 1; }
		printf("HW_PARAMS con buffer de %u B (2 periodos de %u B)...\n", bytes, bytes / 2); fflush(stdout);
		if (ioctl(fd, SNDRV_PCM_IOCTL_HW_PARAMS, &q) < 0) { perror("HW_PARAMS"); return 1; }
		printf("HW_PARAMS ok:\n"); show(&q);
		ioctl(fd, SNDRV_PCM_IOCTL_HW_FREE);
		printf("HW_FREE ok\n");
	}
	close(fd);
	return 0;
}
