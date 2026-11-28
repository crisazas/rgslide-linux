/* rgslide-audiod: vuelca una FIFO de PCM crudo (S16_LE estereo 48000 Hz) al PCM de la Slide (pcmC0D2p) con las reglas medidas:
 * buffer 96000 B, NO_PERIOD_WAKEUP, START manual y pulso por STATUS (sin IRQ de periodo). Abre el PCM al llegar datos y lo cierra tras 3 s sin ellos.
 * Un hilo lector vacia SIEMPRE la FIFO a un anillo (si no, el escritor se bloquea mientras se abre el PCM, ~2,5 s), de modo que el juego nunca se congela.
 * Uso: rgslide-audiod [FIFO]    (por defecto /run/rgslide/audio.fifo; rgslide-audio-spk on/off enciende y apaga la ruta del altavoz) */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <sound/asound.h>
#define IMASK(p, n) (&(p)->masks[(n) - SNDRV_PCM_HW_PARAM_FIRST_MASK])
#define IVAL(p, n) (&(p)->intervals[(n) - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL])
static void full(struct snd_pcm_hw_params *p){memset(p,0,sizeof*p);for(int i=0;i<=SNDRV_PCM_HW_PARAM_LAST_MASK-SNDRV_PCM_HW_PARAM_FIRST_MASK;i++)memset(&p->masks[i],0xff,sizeof p->masks[i]);for(int i=0;i<=SNDRV_PCM_HW_PARAM_LAST_INTERVAL-SNDRV_PCM_HW_PARAM_FIRST_INTERVAL;i++){p->intervals[i].min=0;p->intervals[i].max=~0U;}p->rmask=~0U;p->info=~0U;}
static void sm(struct snd_pcm_hw_params *p,int n,unsigned b){struct snd_mask*m=IMASK(p,n);memset(m,0,sizeof*m);m->bits[b>>5]=1U<<(b&31);}
static void si(struct snd_pcm_hw_params *p,int n,unsigned v){struct snd_interval*i=IVAL(p,n);i->min=i->max=v;i->integer=1;i->openmin=i->openmax=0;}
#define CH 480
#define TARGET 4800
#define RING 192000
#define LAT 9600
#define OPENKEEP 48000      /* al abrir se conserva hasta 1 s de audio ya recibido (clics cortos) */
#define HARDTRIM 144000     /* si el anillo pasa de 3 s se recorta a 1 s */
#define IDLE_PCM_MS 15000   /* tras el ultimo sonido el PCM sigue abierto este tiempo (baja latencia en menus) */
#define IDLE_ROUTE_MS 180000 /* y la ruta del altavoz se apaga tras este reposo total */
#define BUFFR 24000
#define MUTE_MS 1200        /* con AMP_MUTE=1: al dejar de llegar sonido se quita Speaker Mute (amplificador encendido) */
#define SILMS IDLE_PCM_MS   /* silencio digital seguido tras el cual se cierra el PCM y se apaga el amplificador */
#ifndef F_SETPIPE_SZ
#define F_SETPIPE_SZ 1031
#endif
static volatile int stop;
static void onsig(int s){(void)s;stop=1;}
static long now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000L+t.tv_nsec/1000000L;}

static short ring[RING*2]; static long rhead, rtail; /* en frames, crecen sin parar */
static pthread_mutex_t mu=PTHREAD_MUTEX_INITIALIZER; static volatile long last_data, last_sound, last_hw_ms;
static volatile int pcm_active;
static int fifo_rd;
static int always_on, amp_mute;   /* AMP_MUTE=1: con silencio digital se activa "Speaker Mute" (apaga el amplificador, GPIO 176) y se quita al llegar sonido */
static int mute_state=0, ctl_fd=-1; static unsigned mute_numid;   /* /etc/rgslide/audio.conf: ALWAYS_ON=1 mantiene la ruta y el PCM abiertos (sin latencia de apertura) */
static void load_conf(void){FILE*f=fopen("/etc/rgslide/audio.conf","r");char l[128];always_on=0;if(!f)return;while(fgets(l,sizeof l,f)){if(!strncmp(l,"ALWAYS_ON=1",11))always_on=1; if(!strncmp(l,"AMP_MUTE=1",10))amp_mute=1;}fclose(f);}
static volatile long st_under, st_drop, st_in, st_out;
static long ring_frames(void){return rtail-rhead;}
static void ring_put(const short*b,int fr)
{
	pthread_mutex_lock(&mu);
	for(int i=0;i<fr;i++){ring[(rtail%RING)*2]=b[2*i];ring[(rtail%RING)*2+1]=b[2*i+1];rtail++;}
	if(rtail-rhead>RING)rhead=rtail-RING;
	st_in+=fr;
	pthread_mutex_unlock(&mu);
}
static int ring_get(short*b,int fr)
{
	pthread_mutex_lock(&mu);
	/* con atraso, descarta solo frames de silencio digital del principio (nunca audio audible) */
	while(rtail-rhead>LAT*2){short l=ring[(rhead%RING)*2],r=ring[(rhead%RING)*2+1];if(l>64||l<-64||r>64||r<-64)break;rhead++;}
	long n=rtail-rhead; if(n>fr)n=fr;
	for(long i=0;i<n;i++){b[2*i]=ring[(rhead%RING)*2];b[2*i+1]=ring[(rhead%RING)*2+1];rhead++;}
	pthread_mutex_unlock(&mu);
	return (int)n;
}
static void ring_trim(long keep){pthread_mutex_lock(&mu);if(rtail-rhead>keep)rhead=rtail-keep;pthread_mutex_unlock(&mu);}
static void *reader(void*a)
{
	(void)a; static unsigned char raw[4096]; int carry=0;
	while(!stop){
		/* Con el PCM ya abierto y avanzando (hw_ptr vivo <250 ms), aplicar contrapresion natural por la FIFO
		 * si el anillo supera LAT (200 ms): asi los clientes ALSA sobre slave "null" se regulan a 48 kHz reales.
		 * Si el DSP se atasca (hw_ptr detenido >=250 ms), drenar SIEMPRE la FIFO para no bloquear nunca a ES-DE/juegos. */
		if(pcm_active && (now_ms()-last_hw_ms<250) && ring_frames()>LAT){usleep(4000);continue;}
		int n=read(fifo_rd,raw+carry,sizeof raw-carry);
		if(n<=0){usleep(5000);continue;}
		n+=carry; int fr=n/4; ring_put((short*)raw,fr); carry=n-fr*4; if(carry)memmove(raw,raw+fr*4,carry);
		last_data=now_ms();
		short*sp=(short*)raw; for(int i=0;i<fr*2;i++) if(sp[i]>64||sp[i]<-64){last_sound=last_data;break;}
	}
	return 0;
}
static int pcm_open(void)
{
	int fd=open("/dev/snd/pcmC0D2p",O_RDWR); if(fd<0)return -1;
	struct snd_pcm_hw_params p; full(&p);
	sm(&p,SNDRV_PCM_HW_PARAM_ACCESS,SNDRV_PCM_ACCESS_RW_INTERLEAVED); sm(&p,SNDRV_PCM_HW_PARAM_FORMAT,SNDRV_PCM_FORMAT_S16_LE);
	si(&p,SNDRV_PCM_HW_PARAM_CHANNELS,2); si(&p,SNDRV_PCM_HW_PARAM_RATE,48000); si(&p,SNDRV_PCM_HW_PARAM_PERIOD_BYTES,48000); si(&p,SNDRV_PCM_HW_PARAM_BUFFER_BYTES,96000);
	if(ioctl(fd,SNDRV_PCM_IOCTL_HW_REFINE,&p)<0||IVAL(&p,SNDRV_PCM_HW_PARAM_BUFFER_BYTES)->min>96000){close(fd);return -1;}
	p.flags|=SNDRV_PCM_HW_PARAMS_NO_PERIOD_WAKEUP;
	if(ioctl(fd,SNDRV_PCM_IOCTL_HW_PARAMS,&p)<0){close(fd);return -1;}
	struct snd_pcm_sw_params sw; memset(&sw,0,sizeof sw);
	sw.tstamp_mode=SNDRV_PCM_TSTAMP_NONE; sw.period_step=1; sw.avail_min=CH; sw.start_threshold=1000000; sw.stop_threshold=24000*1000;
	if(ioctl(fd,SNDRV_PCM_IOCTL_SW_PARAMS,&sw)<0||ioctl(fd,SNDRV_PCM_IOCTL_PREPARE)<0){close(fd);return -1;}
	return fd;
}
static void pcm_close(int fd){ioctl(fd,SNDRV_PCM_IOCTL_DROP);ioctl(fd,SNDRV_PCM_IOCTL_HW_FREE);close(fd);}
static long avail(int fd,unsigned long*hw){struct snd_pcm_status st;memset(&st,0,sizeof st);if(ioctl(fd,SNDRV_PCM_IOCTL_STATUS,&st)<0)return -1;if(hw)*hw=st.hw_ptr;return (long)st.avail;}
static int wr(int fd,short*b,int frames){struct snd_xferi x={.buf=b,.frames=frames};return ioctl(fd,SNDRV_PCM_IOCTL_WRITEI_FRAMES,&x);}
static int ctl_open(void){ if(ctl_fd>=0)return 0; ctl_fd=open("/dev/snd/controlC0",O_RDWR); return ctl_fd<0?-1:0; }
static unsigned find_numid(const char*name){
	struct snd_ctl_elem_list l; memset(&l,0,sizeof l); if(ioctl(ctl_fd,SNDRV_CTL_IOCTL_ELEM_LIST,&l)<0)return 0;
	unsigned tot=l.count; struct snd_ctl_elem_id*ids=calloc(tot,sizeof *ids); if(!ids)return 0;
	memset(&l,0,sizeof l); l.space=tot; l.pids=ids; unsigned r=0;
	if(ioctl(ctl_fd,SNDRV_CTL_IOCTL_ELEM_LIST,&l)==0) for(unsigned i=0;i<l.used;i++) if(!strcmp((char*)ids[i].name,name)){r=ids[i].numid;break;}
	free(ids); return r;
}
static void set_mute_now(int v){
	if(!amp_mute||mute_state==v)return; if(ctl_open()<0)return;
	if(!mute_numid&&!(mute_numid=find_numid("Speaker Mute")))return;
	struct snd_ctl_elem_value ev; memset(&ev,0,sizeof ev); ev.id.numid=mute_numid; ev.value.integer.value[0]=v;
	if(ioctl(ctl_fd,SNDRV_CTL_IOCTL_ELEM_WRITE,&ev)==0){mute_state=v;fprintf(stderr,"Speaker Mute=%d\n",v);}
}
static void route(const char*m){char c[96];snprintf(c,sizeof c,"/usr/local/sbin/rgslide-audio-spk %s >/dev/null 2>&1",m);if(system(c)){}}

int main(int argc,char**argv)
{
	const char*path=argc>1?argv[1]:"/run/rgslide/audio.fifo";
	signal(SIGTERM,onsig); signal(SIGINT,onsig); load_conf(); fprintf(stderr,"ALWAYS_ON=%d\n",always_on);
	mkdir("/run/rgslide",0755); umask(0); mkfifo(path,0666);
	fifo_rd=open(path,O_RDONLY|O_NONBLOCK); int dummy=open(path,O_WRONLY|O_NONBLOCK); (void)dummy;
	if(fifo_rd<0){perror("fifo");return 1;}
	fcntl(fifo_rd,F_SETPIPE_SZ,4096);
	pthread_t th; pthread_create(&th,0,reader,0);
	static short buf[CH*2], zero[CH*2]; long closed_at=0; int route_on=0;
	/* Cebado: la 1a apertura del PCM tras encender la ruta se atasca siempre (medido, docs 5n). Se hace aqui, una vez,
	 * con la ruta encendida; despues la apertura real tarda ~1,3 s en vez de ~3,5 s. El amplificador solo suena mientras hay PCM abierto. */
	{ route("on"); route_on=1;   /* (con ALWAYS_ON el bucle principal abre el PCM de inmediato y no lo cierra) */
	  for(int t=0;t<3&&!stop;t++){
		int f=pcm_open(); if(f<0){usleep(300000);continue;}
		for(int k=0;k<3;k++)wr(f,zero,CH);
		ioctl(f,SNDRV_PCM_IOCTL_START);
		unsigned long h0=0,h1=0; avail(f,&h0); usleep(250000); avail(f,&h1);
		pcm_close(f);
		if(h1!=h0){fprintf(stderr,"cebado listo (intento %d)\n",t);break;}
		fprintf(stderr,"cebado: hw_ptr clavado (intento %d)\n",t);
	  }
	  closed_at=now_ms(); }
	while(!stop){
		if(!always_on&&(ring_frames()==0||last_sound<=closed_at)){   /* la deteccion de sonido real marca last_sound en el hilo lector */
			usleep(20000); if(ring_frames()>RING/2)ring_trim(OPENKEEP);
			if(route_on&&now_ms()-closed_at>IDLE_ROUTE_MS){route("off");route_on=0;fprintf(stderr,"ruta apagada por reposo\n");}
			continue;
		}   /* solo abre con sonido real: el silencio digital deja el amplificador apagado (sisa encendido) */
		if(!route_on){route("on");route_on=1;}
		int fd=-1;
		for(int t=0;t<4&&fd<0&&!stop;t++){
			int f=pcm_open(); if(f<0){usleep(300000);continue;}
			for(int k=0;k<3;k++)wr(f,zero,CH);
			ioctl(f,SNDRV_PCM_IOCTL_START);
			unsigned long h0=0,h1=0; avail(f,&h0); usleep(250000); avail(f,&h1);
			if(h1!=h0)fd=f; else {fprintf(stderr,"hw_ptr clavado, reabro (intento %d)\n",t);pcm_close(f);}
		}
		if(fd<0){route("off");route_on=0;usleep(500000);continue;}
		ring_trim(OPENKEEP); last_hw_ms=now_ms(); pcm_active=1; last_data=now_ms(); long lastlog=now_ms(); st_under=st_drop=st_in=st_out=0; fprintf(stderr,"audio abierto (anillo=%ld frames, %ld ms desde el primer sonido)\n",ring_frames(),now_ms()-last_sound);
		if(amp_mute&&now_ms()-last_sound>IDLE_PCM_MS)set_mute_now(1);
		unsigned long prev_hw=0; int have_hw=0;
		while(!stop){
			unsigned long hw; long av=avail(fd,&hw); if(av<0)break;
			long now=now_ms();
			if(!have_hw || hw!=prev_hw){prev_hw=hw; have_hw=1; last_hw_ms=now;}
			else if(now-last_hw_ms>500){fprintf(stderr,"hw_ptr atascado (%lu), reabriendo PCM\n",hw); break;}
			long queued=BUFFR-av;
			if(queued<TARGET&&av>=CH){
				if(ring_frames()>HARDTRIM){st_drop+=ring_frames()-OPENKEEP;ring_trim(OPENKEEP);}   /* el juego va un poco mas rapido que el DAC: descarta lo viejo */
				int n=ring_get(buf,CH);
				if(n>0){if(wr(fd,buf,n)<0)break;st_out+=n;}
				else if(queued<CH){st_under++;if(wr(fd,zero,CH)<0)break;}   /* relleno solo si el buffer esta a punto de vaciarse */
				else usleep(4000);
			} else usleep(4000);
			if(amp_mute){ if(now-last_sound>IDLE_PCM_MS&&BUFFR-av<CH*8)set_mute_now(1); else if(now-last_sound<MUTE_MS)set_mute_now(0); }   /* cierra tras 15 s de silencio con la cola consumida (pausas musicales no lo cierran); abre al llegar sonido */
			if(!always_on&&now-last_sound>SILMS)break;
			if(now-lastlog>5000){lastlog=now;fprintf(stderr,"in=%ld out=%ld vaciados=%ld descartados=%ld anillo=%ld cola=%ld\n",st_in,st_out,st_under,st_drop,ring_frames(),BUFFR-av);}
		}
		pcm_active=0; pcm_close(fd); set_mute_now(0); closed_at=now_ms(); fprintf(stderr,"audio cerrado\n");
	}
	return 0;
}
