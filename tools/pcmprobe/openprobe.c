/* openprobe: mide si el PCM arranca (hw_ptr avanza) con variantes de apertura.
 * (cabecera heredada de seqplay) seqplay: reproduce una secuencia de tonos "FREC:AMP:SEG,..." (FREC 0 = silencio) por un PCM abierto UNA vez; un seno por un PCM ALSA con ioctls crudos. Buffer SIEMPRE <= 96000 B (2 x 48000 B, unico valor seguro verificado
 * ademas de 320/38400 en sprd-pcm; ver docs/audio-wifi.md). Uso: sine /dev/snd/pcmC0D2p [segundos] */
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <sound/asound.h>
#define IMASK(p, n) (&(p)->masks[(n) - SNDRV_PCM_HW_PARAM_FIRST_MASK])
#define IVAL(p, n) (&(p)->intervals[(n) - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL])
static void full(struct snd_pcm_hw_params *p){memset(p,0,sizeof*p);for(int i=0;i<=SNDRV_PCM_HW_PARAM_LAST_MASK-SNDRV_PCM_HW_PARAM_FIRST_MASK;i++)memset(&p->masks[i],0xff,sizeof p->masks[i]);for(int i=0;i<=SNDRV_PCM_HW_PARAM_LAST_INTERVAL-SNDRV_PCM_HW_PARAM_FIRST_INTERVAL;i++){p->intervals[i].min=0;p->intervals[i].max=~0U;}p->rmask=~0U;p->info=~0U;}
static void sm(struct snd_pcm_hw_params *p,int n,unsigned b){struct snd_mask*m=IMASK(p,n);memset(m,0,sizeof*m);m->bits[b>>5]=1U<<(b&31);}
static void si(struct snd_pcm_hw_params *p,int n,unsigned v){struct snd_interval*i=IVAL(p,n);i->min=i->max=v;i->integer=1;i->openmin=i->openmax=0;}
static long now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000L+t.tv_nsec/1000000L;}
static int open_pcm(void)
{
	int fd=open("/dev/snd/pcmC0D2p",O_RDWR); if(fd<0)return -1;
	struct snd_pcm_hw_params p; full(&p);
	sm(&p,SNDRV_PCM_HW_PARAM_ACCESS,SNDRV_PCM_ACCESS_RW_INTERLEAVED); sm(&p,SNDRV_PCM_HW_PARAM_FORMAT,SNDRV_PCM_FORMAT_S16_LE);
	si(&p,SNDRV_PCM_HW_PARAM_CHANNELS,2); si(&p,SNDRV_PCM_HW_PARAM_RATE,48000); si(&p,SNDRV_PCM_HW_PARAM_PERIOD_BYTES,48000); si(&p,SNDRV_PCM_HW_PARAM_BUFFER_BYTES,96000);
	if(ioctl(fd,SNDRV_PCM_IOCTL_HW_REFINE,&p)<0){close(fd);return -1;}
	p.flags|=SNDRV_PCM_HW_PARAMS_NO_PERIOD_WAKEUP;
	if(ioctl(fd,SNDRV_PCM_IOCTL_HW_PARAMS,&p)<0){close(fd);return -1;}
	struct snd_pcm_sw_params sw; memset(&sw,0,sizeof sw);
	sw.tstamp_mode=SNDRV_PCM_TSTAMP_NONE; sw.period_step=1; sw.avail_min=480; sw.start_threshold=1000000; sw.stop_threshold=24000*1000;
	if(ioctl(fd,SNDRV_PCM_IOCTL_SW_PARAMS,&sw)<0||ioctl(fd,SNDRV_PCM_IOCTL_PREPARE)<0){close(fd);return -1;}
	return fd;
}
static unsigned long hw(int fd,long*av){struct snd_pcm_status st;memset(&st,0,sizeof st);ioctl(fd,SNDRV_PCM_IOCTL_STATUS,&st);if(av)*av=st.avail;return st.hw_ptr;}
static void fill(int fd,int frames){short z[480*2];memset(z,0,sizeof z);for(int k=0;k<frames/480;k++){struct snd_xferi x={.buf=z,.frames=480};ioctl(fd,SNDRV_PCM_IOCTL_WRITEI_FRAMES,&x);}}
/* devuelve ms hasta que hw_ptr avanza >3000 bytes, o -1 */
static long run(int fd,int prefill,int predelay_ms,int timeout_ms)
{
	fill(fd,prefill);
	if(predelay_ms)usleep(predelay_ms*1000);
	long t0=now_ms(); ioctl(fd,SNDRV_PCM_IOCTL_START);
	unsigned long h0=hw(fd,0);
	while(now_ms()-t0<timeout_ms){usleep(20000);if(hw(fd,0)>h0+3000)return now_ms()-t0;}
	return -1;
}
int main(int argc,char**argv)
{
	/* uso: openprobe MODO PREFILL PREDELAY_MS   MODO: 0=abrir/arrancar; 1=si se atasca DROP+PREPARE sin cerrar; 2=si se atasca cerrar y reabrir */
	int mode=argc>1?atoi(argv[1]):0, prefill=argc>2?atoi(argv[2]):14400, pre=argc>3?atoi(argv[3]):0;
	long t0=now_ms(); int fd=open_pcm(); if(fd<0){puts("OPEN_FALLO");return 1;}
	long r=run(fd,prefill,pre,1500); long total=now_ms()-t0;
	printf("intento1: %s (%ld ms desde la apertura)\n", r>=0?"OK":"ATASCADO", total);
	if(r<0&&mode==1){
		ioctl(fd,SNDRV_PCM_IOCTL_DROP); ioctl(fd,SNDRV_PCM_IOCTL_PREPARE);
		r=run(fd,prefill,pre,1500); printf("DROP+PREPARE: %s (%ld ms desde la apertura)\n",r>=0?"OK":"ATASCADO",now_ms()-t0);
	}
	if(r<0&&mode==2){
		ioctl(fd,SNDRV_PCM_IOCTL_DROP); ioctl(fd,SNDRV_PCM_IOCTL_HW_FREE); close(fd);
		fd=open_pcm(); r=run(fd,prefill,pre,1500); printf("reabrir: %s (%ld ms desde la apertura)\n",r>=0?"OK":"ATASCADO",now_ms()-t0);
	}
	ioctl(fd,SNDRV_PCM_IOCTL_DROP); ioctl(fd,SNDRV_PCM_IOCTL_HW_FREE); close(fd);
	return r>=0?0:1;
}
