/* sine: reproduce un seno por un PCM ALSA con ioctls crudos. Buffer SIEMPRE <= 96000 B (2 x 48000 B, unico valor seguro verificado
 * ademas de 320/38400 en sprd-pcm; ver docs/audio-wifi.md). Uso: sine /dev/snd/pcmC0D2p [segundos] */
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sound/asound.h>
#define IMASK(p, n) (&(p)->masks[(n) - SNDRV_PCM_HW_PARAM_FIRST_MASK])
#define IVAL(p, n) (&(p)->intervals[(n) - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL])
static void full(struct snd_pcm_hw_params *p){memset(p,0,sizeof*p);for(int i=0;i<=SNDRV_PCM_HW_PARAM_LAST_MASK-SNDRV_PCM_HW_PARAM_FIRST_MASK;i++)memset(&p->masks[i],0xff,sizeof p->masks[i]);for(int i=0;i<=SNDRV_PCM_HW_PARAM_LAST_INTERVAL-SNDRV_PCM_HW_PARAM_FIRST_INTERVAL;i++){p->intervals[i].min=0;p->intervals[i].max=~0U;}p->rmask=~0U;p->info=~0U;}
static void sm(struct snd_pcm_hw_params *p,int n,unsigned b){struct snd_mask*m=IMASK(p,n);memset(m,0,sizeof*m);m->bits[b>>5]=1U<<(b&31);}
static void si(struct snd_pcm_hw_params *p,int n,unsigned v){struct snd_interval*i=IVAL(p,n);i->min=i->max=v;i->integer=1;i->openmin=i->openmax=0;}
int main(int argc,char**argv)
{
	if(argc<2)return 2;
	int secs=argc>2?atoi(argv[2]):2;
	int fd=open(argv[1],O_RDWR); if(fd<0){perror("open");return 1;}
	struct snd_pcm_hw_params p; full(&p);
	sm(&p,SNDRV_PCM_HW_PARAM_ACCESS,SNDRV_PCM_ACCESS_RW_INTERLEAVED); sm(&p,SNDRV_PCM_HW_PARAM_FORMAT,SNDRV_PCM_FORMAT_S16_LE);
	si(&p,SNDRV_PCM_HW_PARAM_CHANNELS,2); si(&p,SNDRV_PCM_HW_PARAM_RATE,48000); si(&p,SNDRV_PCM_HW_PARAM_PERIOD_BYTES,48000); si(&p,SNDRV_PCM_HW_PARAM_PERIODS,2);
	if(ioctl(fd,SNDRV_PCM_IOCTL_HW_REFINE,&p)<0){perror("HW_REFINE");return 1;}
	unsigned bb=IVAL(&p,SNDRV_PCM_HW_PARAM_BUFFER_BYTES)->min;
	if(bb>96000){fprintf(stderr,"buffer %u > 96000: no lo pido\n",bb);return 1;}
	printf("HW_PARAMS buffer=%u B\n",bb);fflush(stdout);
	if(ioctl(fd,SNDRV_PCM_IOCTL_HW_PARAMS,&p)<0){perror("HW_PARAMS");return 1;}
	struct snd_pcm_sw_params sw; memset(&sw,0,sizeof sw);
	sw.tstamp_mode=SNDRV_PCM_TSTAMP_NONE; sw.period_step=1; sw.avail_min=12000; sw.start_threshold=12000; sw.stop_threshold=24000*100; sw.boundary=24000*1000;
	if(ioctl(fd,SNDRV_PCM_IOCTL_SW_PARAMS,&sw)<0){perror("SW_PARAMS");return 1;}
	if(ioctl(fd,SNDRV_PCM_IOCTL_PREPARE)<0){perror("PREPARE");return 1;}
	printf("PREPARE ok, escribo %d s\n",secs);fflush(stdout);
	short buf[2*4800]; double ph=0; int total=secs*10; int err=0;
	for(int n=0;n<total;n++){
		for(int i=0;i<4800;i++){short v=(short)(sin(ph)*8000);ph+=2*M_PI*440.0/48000.0;buf[2*i]=buf[2*i+1]=v;}
		struct snd_xferi x={.buf=buf,.frames=4800};
		if(ioctl(fd,SNDRV_PCM_IOCTL_WRITEI_FRAMES,&x)<0){perror("WRITEI");err=1;break;}
		if(n==0){printf("primer WRITEI ok (%ld frames)\n",(long)x.result);fflush(stdout);}
	}
	ioctl(fd,SNDRV_PCM_IOCTL_DRAIN); ioctl(fd,SNDRV_PCM_IOCTL_HW_FREE);
	printf(err?"FIN con error\n":"FIN ok\n"); return err;
}
