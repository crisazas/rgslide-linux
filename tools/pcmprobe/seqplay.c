/* seqplay: reproduce una secuencia de tonos "FREC:AMP:SEG,..." (FREC 0 = silencio) por un PCM abierto UNA vez; un seno por un PCM ALSA con ioctls crudos. Buffer SIEMPRE <= 96000 B (2 x 48000 B, unico valor seguro verificado
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
int main(int argc,char**argv)
{
	if(argc<3)return 2;
	int fd=open(argv[1],O_RDWR); if(fd<0){perror("open");return 1;}
	struct snd_pcm_hw_params p; full(&p);
	sm(&p,SNDRV_PCM_HW_PARAM_ACCESS,SNDRV_PCM_ACCESS_RW_INTERLEAVED); sm(&p,SNDRV_PCM_HW_PARAM_FORMAT,SNDRV_PCM_FORMAT_S16_LE);
	si(&p,SNDRV_PCM_HW_PARAM_CHANNELS,2); si(&p,SNDRV_PCM_HW_PARAM_RATE,48000); si(&p,SNDRV_PCM_HW_PARAM_PERIOD_BYTES,48000); si(&p,SNDRV_PCM_HW_PARAM_BUFFER_BYTES,96000);
	if(ioctl(fd,SNDRV_PCM_IOCTL_HW_REFINE,&p)<0||IVAL(&p,SNDRV_PCM_HW_PARAM_BUFFER_BYTES)->min>96000){perror("REFINE");return 1;}
	p.flags|=SNDRV_PCM_HW_PARAMS_NO_PERIOD_WAKEUP;
	if(ioctl(fd,SNDRV_PCM_IOCTL_HW_PARAMS,&p)<0){perror("HW_PARAMS");return 1;}
	struct snd_pcm_sw_params sw; memset(&sw,0,sizeof sw);
	sw.tstamp_mode=SNDRV_PCM_TSTAMP_NONE; sw.period_step=1; sw.avail_min=480; sw.start_threshold=1000000; sw.stop_threshold=24000*1000;
	if(ioctl(fd,SNDRV_PCM_IOCTL_SW_PARAMS,&sw)<0||ioctl(fd,SNDRV_PCM_IOCTL_PREPARE)<0){perror("SW/PREPARE");return 1;}
	short buf[480*2]; double ph=0; int started=0; long sent=0, t0ms=0;
	struct timespec ts0; clock_gettime(CLOCK_MONOTONIC,&ts0);
	char*list=strdup(argv[2]); char*save=0;
	for(char*tok=strtok_r(list,",",&save);tok;tok=strtok_r(0,",",&save)){
		double f=0,amp=0,dur=0; sscanf(tok,"%lf:%lf:%lf",&f,&amp,&dur);
		long frames=(long)(dur*48000);
		printf("SEG f=%.0f amp=%.0f dur=%.1f en_frame=%ld\n",f,amp,dur,sent);fflush(stdout);
		for(long n=0;n<frames;n+=480){
			struct snd_pcm_status st; 
			for(;;){memset(&st,0,sizeof st);if(ioctl(fd,SNDRV_PCM_IOCTL_STATUS,&st)<0){perror("STATUS");return 1;}if((long)st.avail>=480)break;usleep(4000);}
			for(int i=0;i<480;i++){short v=f>0?(short)(sin(ph)*amp):0;if(f>0)ph+=2*M_PI*f/48000.0;buf[2*i]=buf[2*i+1]=v;}
			struct snd_xferi x={.buf=buf,.frames=480};
			if(ioctl(fd,SNDRV_PCM_IOCTL_WRITEI_FRAMES,&x)<0){perror("WRITEI");return 1;}
			sent+=480;
			if(!started&&sent>=14400){ioctl(fd,SNDRV_PCM_IOCTL_START);started=1;}
		}
	}
	usleep(300000); ioctl(fd,SNDRV_PCM_IOCTL_DROP); ioctl(fd,SNDRV_PCM_IOCTL_HW_FREE); (void)t0ms;
	printf("FIN %ld frames\n",sent); return 0;
}
