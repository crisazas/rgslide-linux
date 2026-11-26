/* cap: captura de un PCM ALSA con ioctls crudos y buffer acotado (<= 7680 B: 2 x 3840 B; mas puede provocar panic en sprd-pcm, docs/agent-audio-research.md 4.1).
 * Uso: cap /dev/snd/pcmC0D4c refine            -> solo HW_REFINE (no reserva memoria)
 *      cap /dev/snd/pcmC0D4c SEGUNDOS SALIDA   -> captura mono S16_LE 48000 Hz; imprime el RMS y guarda el PCM crudo */
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
#define IVAL(p, n) (&(p)->intervals[(n) - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL])
static void full(struct snd_pcm_hw_params *p){memset(p,0,sizeof*p);for(int i=0;i<=SNDRV_PCM_HW_PARAM_LAST_MASK-SNDRV_PCM_HW_PARAM_FIRST_MASK;i++)memset(&p->masks[i],0xff,sizeof p->masks[i]);for(int i=0;i<=SNDRV_PCM_HW_PARAM_LAST_INTERVAL-SNDRV_PCM_HW_PARAM_FIRST_INTERVAL;i++){p->intervals[i].min=0;p->intervals[i].max=~0U;}p->rmask=~0U;p->info=~0U;}
static void sm(struct snd_pcm_hw_params *p,int n,unsigned b){struct snd_mask*m=IMASK(p,n);memset(m,0,sizeof*m);m->bits[b>>5]=1U<<(b&31);}
static void si(struct snd_pcm_hw_params *p,int n,unsigned v){struct snd_interval*i=IVAL(p,n);i->min=i->max=v;i->integer=1;i->openmin=i->openmax=0;}
int main(int argc,char**argv)
{
	if(argc<3)return 2;
	int refine=!strcmp(argv[2],"refine");
	int fd=open(argv[1],O_RDWR); if(fd<0){perror("open");return 1;}
	struct snd_pcm_hw_params p; full(&p);
	sm(&p,SNDRV_PCM_HW_PARAM_ACCESS,SNDRV_PCM_ACCESS_RW_INTERLEAVED); sm(&p,SNDRV_PCM_HW_PARAM_FORMAT,SNDRV_PCM_FORMAT_S16_LE);
	si(&p,SNDRV_PCM_HW_PARAM_CHANNELS,1); si(&p,SNDRV_PCM_HW_PARAM_RATE,48000); si(&p,SNDRV_PCM_HW_PARAM_PERIOD_BYTES,3840); si(&p,SNDRV_PCM_HW_PARAM_BUFFER_BYTES,7680);
	if(ioctl(fd,SNDRV_PCM_IOCTL_HW_REFINE,&p)<0){perror("HW_REFINE");return 1;}
	unsigned bb=IVAL(&p,SNDRV_PCM_HW_PARAM_BUFFER_BYTES)->min, pb=IVAL(&p,SNDRV_PCM_HW_PARAM_PERIOD_BYTES)->min;
	printf("REFINE ok: buffer=%u..%u periodo=%u..%u canales=%u..%u\n",bb,IVAL(&p,SNDRV_PCM_HW_PARAM_BUFFER_BYTES)->max,pb,IVAL(&p,SNDRV_PCM_HW_PARAM_PERIOD_BYTES)->max,IVAL(&p,SNDRV_PCM_HW_PARAM_CHANNELS)->min,IVAL(&p,SNDRV_PCM_HW_PARAM_CHANNELS)->max);fflush(stdout);
	if(refine)return 0;
	if(bb>7680){fprintf(stderr,"buffer %u > 7680: no lo pido\n",bb);return 1;}
	int secs=atoi(argv[2]); FILE*out=fopen(argv[3],"wb"); int nowake=argc>4&&!strcmp(argv[4],"nowake");
	if(nowake)p.flags|=SNDRV_PCM_HW_PARAMS_NO_PERIOD_WAKEUP;
	if(ioctl(fd,SNDRV_PCM_IOCTL_HW_PARAMS,&p)<0){perror("HW_PARAMS");return 1;}
	struct snd_pcm_sw_params sw; memset(&sw,0,sizeof sw);
	sw.tstamp_mode=SNDRV_PCM_TSTAMP_NONE; sw.period_step=1; sw.avail_min=1920; sw.start_threshold=1; sw.stop_threshold=1920*1000;
	if(ioctl(fd,SNDRV_PCM_IOCTL_SW_PARAMS,&sw)<0){perror("SW_PARAMS");return 1;}
	if(ioctl(fd,SNDRV_PCM_IOCTL_PREPARE)<0){perror("PREPARE");return 1;}
	printf("PREPARE ok, capturo %d s\n",secs);fflush(stdout);
	if(ioctl(fd,SNDRV_PCM_IOCTL_START)<0){perror("START");}
	for(int k=0;k<6;k++){struct snd_pcm_status st;memset(&st,0,sizeof st);usleep(100000);if(ioctl(fd,SNDRV_PCM_IOCTL_STATUS,&st)<0){perror("STATUS");break;}printf("t=%d00ms estado=%d hw_ptr=%lu avail=%lu\n",k+1,(int)st.state,(unsigned long)st.hw_ptr,(unsigned long)st.avail);fflush(stdout);}
	short buf[960]; double acc=0; long n=0; int err=0; int mn=32767,mx=-32768; long got=0; long want=(long)secs*48000; int idle=0;
	while(got<want){
		struct snd_pcm_status st; memset(&st,0,sizeof st);
		if(ioctl(fd,SNDRV_PCM_IOCTL_STATUS,&st)<0){perror("STATUS");err=1;break;}
		if((long)st.avail<960){usleep(8000); if(++idle>1000){fprintf(stderr,"sin datos\n");err=1;break;} continue;}
		idle=0;
		struct snd_xferi x={.buf=buf,.frames=960};
		if(ioctl(fd,SNDRV_PCM_IOCTL_READI_FRAMES,&x)<0){perror("READI");err=1;break;}
		for(int i=0;i<x.result;i++){acc+=(double)buf[i]*buf[i];n++;if(buf[i]<mn)mn=buf[i];if(buf[i]>mx)mx=buf[i];}
		if(out)fwrite(buf,2,x.result,out);
		got+=x.result;
		if(got==x.result){printf("primer READI ok (%ld frames)\n",(long)x.result);fflush(stdout);}
	}
	if(out)fclose(out);
	ioctl(fd,SNDRV_PCM_IOCTL_DROP); ioctl(fd,SNDRV_PCM_IOCTL_HW_FREE);
	printf("muestras=%ld RMS=%.1f min=%d max=%d\n",n,n?sqrt(acc/n):0.0,mn,mx);
	printf(err?"FIN con error\n":"FIN ok\n"); return err;
}
