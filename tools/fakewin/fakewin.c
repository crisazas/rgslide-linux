/* Ventana falsa: ANativeWindow propio que reparte AHardwareBuffer reales y cuenta los queueBuffer.
 * Se carga el blob con dlopen y se llama a eglCreateWindowSurface con esta ventana. Solo bionic (NDK). */
#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

#define BLOB "/vendor/lib64/egl/libGLES_mali.so"
static const void *(*p_getNativeHandle)(const AHardwareBuffer *);
#define NBUF 3
#define MAGIC_W (('_' << 24) | ('w' << 16) | ('n' << 8) | 'd')
#define MAGIC_B (('_' << 24) | ('b' << 16) | ('f' << 8) | 'r')

typedef struct { int magic, version; void *reserved[4]; void (*incRef)(void *); void (*decRef)(void *); } base_t;
typedef struct { base_t common; int width, height, stride, format, usage; void *reserved[2]; const void *handle; void *reserved_proc[8]; } wbuf_t;
typedef struct fw fw_t;
struct fw {
	base_t common;
	uint32_t flags;
	int minSwap, maxSwap;
	float xdpi, ydpi;
	intptr_t oem[4];
	int (*setSwapInterval)(fw_t *, int);
	int (*dequeueBuffer_DEPRECATED)(fw_t *, wbuf_t **);
	int (*lockBuffer_DEPRECATED)(fw_t *, wbuf_t *);
	int (*queueBuffer_DEPRECATED)(fw_t *, wbuf_t *);
	int (*query)(const fw_t *, int, int *);
	int (*perform)(fw_t *, int, ...);
	int (*cancelBuffer_DEPRECATED)(fw_t *, wbuf_t *);
	int (*dequeueBuffer)(fw_t *, wbuf_t **, int *);
	int (*queueBuffer)(fw_t *, wbuf_t *, int);
	int (*cancelBuffer)(fw_t *, wbuf_t *, int);
};

static int W = 640, H = 480, FMT = 1;
static uint64_t USAGE = 0;
static wbuf_t *bufs[NBUF];
static AHardwareBuffer *ahb[NBUF];
static int busy[NBUF], nq = 0, nd = 0, ncall = 0;
static int log_perform = 1;

static void inc(void *b) { (void)b; }
static void dec(void *b) { (void)b; }

static int f_swap(fw_t *w, int i) { (void)w; printf("  win: setSwapInterval(%d)\n", i); return 0; }
static int f_query(const fw_t *w, int what, int *v)
{
	(void)w; ncall++;
	switch (what) {
	case 0: *v = W; break;			/* WIDTH */
	case 1: *v = H; break;			/* HEIGHT */
	case 2: *v = FMT; break;		/* FORMAT */
	case 3: *v = 2; break;			/* MIN_UNDEQUEUED_BUFFERS */
	case 4: *v = 0; break;			/* QUEUES_TO_WINDOW_COMPOSER */
	case 5: *v = 1; break;			/* CONCRETE_TYPE = NATIVE_WINDOW_SURFACE */
	case 6: *v = W; break;			/* DEFAULT_WIDTH */
	case 7: *v = H; break;			/* DEFAULT_HEIGHT */
	case 8: *v = 0; break;			/* TRANSFORM_HINT */
	case 9: *v = 0; break;
	case 10: *v = 0; break;			/* CONSUMER_USAGE_BITS */
	case 11: *v = 0; break;
	case 12: *v = 0; break;			/* DEFAULT_DATASPACE */
	case 13: *v = 0; break;			/* BUFFER_AGE */
	default: if (log_perform) printf("  win: query(%d) sin respuesta\n", what); *v = 0; return -1;
	}
	if (log_perform) printf("  win: query(%d) = %d\n", what, *v);
	return 0;
}
static int f_perform(fw_t *w, int op, ...)
{
	__builtin_va_list ap;
	(void)w; ncall++;
	__builtin_va_start(ap, op);
	switch (op) {
	case 0: USAGE = __builtin_va_arg(ap, unsigned); if (log_perform) printf("  win: perform SET_USAGE 0x%llx\n", (unsigned long long)USAGE); break;
	case 8: W = __builtin_va_arg(ap, int); H = __builtin_va_arg(ap, int); if (log_perform) printf("  win: perform SET_BUFFERS_DIMENSIONS %dx%d\n", W, H); break;
	case 9: FMT = __builtin_va_arg(ap, int); printf("  win: perform SET_BUFFERS_FORMAT %d\n", FMT); break;
	case 13: printf("  win: perform API_CONNECT api=%d\n", __builtin_va_arg(ap, int)); break;
	case 14: printf("  win: perform API_DISCONNECT api=%d\n", __builtin_va_arg(ap, int)); break;
	case 4: printf("  win: perform SET_BUFFER_COUNT %d\n", __builtin_va_arg(ap, int)); break;
	case 6: { int t = __builtin_va_arg(ap, int); if (log_perform) printf("  win: perform SET_BUFFERS_TRANSFORM %d\n", t); } break;
	case 19: printf("  win: perform SET_BUFFERS_DATASPACE %d\n", __builtin_va_arg(ap, int)); break;
	case 20: USAGE = __builtin_va_arg(ap, uint64_t); printf("  win: perform SET_USAGE64 0x%llx\n", (unsigned long long)USAGE); break;
	default: printf("  win: perform op=%d (aceptado sin hacer nada)\n", op);
	}
	__builtin_va_end(ap);
	fflush(stdout);
	return 0;
}
static int alloc_all(void)
{
	AHardwareBuffer_Desc d;
	int i;
	for (i = 0; i < NBUF; i++) {
		AHardwareBuffer_Desc o;
		memset(&d, 0, sizeof d);
		d.width = W; d.height = H; d.layers = 1;
		d.format = FMT ? FMT : AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
		d.usage = USAGE | AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT | AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
		if (AHardwareBuffer_allocate(&d, &ahb[i])) { printf("FALLO AHardwareBuffer_allocate %d (%dx%d fmt=%d usage=0x%llx)\n", i, W, H, FMT, (unsigned long long)d.usage); return -1; }
		AHardwareBuffer_describe(ahb[i], &o);
		bufs[i] = calloc(1, sizeof(wbuf_t));
		bufs[i]->common.magic = MAGIC_B; bufs[i]->common.version = sizeof(wbuf_t);
		bufs[i]->common.incRef = inc; bufs[i]->common.decRef = dec;
		bufs[i]->width = o.width; bufs[i]->height = o.height; bufs[i]->stride = o.stride; bufs[i]->format = o.format;
		bufs[i]->usage = (int)o.usage;
		if (!p_getNativeHandle) p_getNativeHandle = dlsym(RTLD_DEFAULT, "AHardwareBuffer_getNativeHandle");
		bufs[i]->handle = p_getNativeHandle ? p_getNativeHandle(ahb[i]) : 0;
		printf("  win: buffer %d %ux%u stride=%u fmt=%u usage=0x%llx handle=%p\n", i, o.width, o.height, o.stride, o.format, (unsigned long long)o.usage, bufs[i]->handle);
	}
	return 0;
}
static int f_dequeue(fw_t *w, wbuf_t **b, int *fence)
{
	int i;
	(void)w; nd++;
	if (!ahb[0] && alloc_all()) return -1;
	for (i = 0; i < NBUF; i++) if (!busy[i]) { busy[i] = 1; *b = bufs[i]; *fence = -1; return 0; }
	return -1;
}
static int f_queue(fw_t *w, wbuf_t *b, int fence)
{
	int i;
	(void)w; (void)fence; nq++;
	for (i = 0; i < NBUF; i++) if (bufs[i] == b) busy[i] = 0;
	return 0;
}
static int f_cancel(fw_t *w, wbuf_t *b, int fence)
{
	int i;
	(void)w; (void)fence;
	for (i = 0; i < NBUF; i++) if (bufs[i] == b) busy[i] = 0;
	return 0;
}

static fw_t win;
static void *lib;
#define LD(n, ret, args) typedef ret (*n##_t) args; n##_t n = (n##_t)dlsym(lib, #n); if (!n) { printf("FALLO: simbolo %s\n", #n); return 2; }

int main(int argc, char **argv)
{
	EGLDisplay dpy;
	EGLConfig cfg;
	EGLSurface surf;
	EGLContext ctx;
	EGLint maj, min, n = 0, id = 0, fr;
	int i;

	if (argc > 2) { W = atoi(argv[1]); H = atoi(argv[2]); }
	lib = dlopen(BLOB, RTLD_NOW);
	if (!lib) { printf("FALLO dlopen: %s\n", dlerror()); return 2; }
	LD(eglGetDisplay, EGLDisplay, (EGLNativeDisplayType));
	LD(eglInitialize, EGLBoolean, (EGLDisplay, EGLint *, EGLint *));
	LD(eglBindAPI, EGLBoolean, (EGLenum));
	LD(eglChooseConfig, EGLBoolean, (EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *));
	LD(eglGetConfigAttrib, EGLBoolean, (EGLDisplay, EGLConfig, EGLint, EGLint *));
	LD(eglCreateWindowSurface, EGLSurface, (EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint *));
	LD(eglCreateContext, EGLContext, (EGLDisplay, EGLConfig, EGLContext, const EGLint *));
	LD(eglMakeCurrent, EGLBoolean, (EGLDisplay, EGLSurface, EGLSurface, EGLContext));
	LD(eglSwapBuffers, EGLBoolean, (EGLDisplay, EGLSurface));
	LD(eglGetError, EGLint, (void));
	LD(glGetString, const GLubyte *, (GLenum));
	LD(glClearColor, void, (GLfloat, GLfloat, GLfloat, GLfloat));
	LD(glClear, void, (GLbitfield));
	LD(glViewport, void, (GLint, GLint, GLsizei, GLsizei));
	LD(glFinish, void, (void));
	LD(glReadPixels, void, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *));

	memset(&win, 0, sizeof win);
	win.common.magic = MAGIC_W; win.common.version = sizeof(fw_t);
	win.common.incRef = (void (*)(void *))inc; win.common.decRef = (void (*)(void *))dec;
	win.minSwap = 0; win.maxSwap = 1; win.xdpi = win.ydpi = 160;
	win.setSwapInterval = f_swap; win.query = f_query; win.perform = f_perform;
	win.dequeueBuffer = f_dequeue; win.queueBuffer = f_queue; win.cancelBuffer = f_cancel;

	dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (!eglInitialize(dpy, &maj, &min)) { printf("FALLO eglInitialize 0x%x\n", eglGetError()); return 2; }
	eglBindAPI(EGL_OPENGL_ES_API);
	{
		EGLint a[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
			       EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
		if (!eglChooseConfig(dpy, a, &cfg, 1, &n) || n < 1) { printf("FALLO: sin config WINDOW_BIT RGBA8 (err 0x%x)\n", eglGetError()); return 2; }
	}
	eglGetConfigAttrib(dpy, cfg, EGL_NATIVE_VISUAL_ID, &id);
	printf("config elegida: EGL_NATIVE_VISUAL_ID=%d\n", id);
	printf("OK: eglChooseConfig con WINDOW_BIT\n"); fflush(stdout);

	surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)&win, 0);
	if (surf == EGL_NO_SURFACE) { printf("FALLO eglCreateWindowSurface err=0x%x (%d llamadas a la ventana)\n", eglGetError(), ncall); return 3; }
	printf("OK: eglCreateWindowSurface\n"); fflush(stdout);
	{
		EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
		ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ca);
		if (ctx == EGL_NO_CONTEXT) { printf("FALLO eglCreateContext 0x%x\n", eglGetError()); return 3; }
		if (!eglMakeCurrent(dpy, surf, surf, ctx)) { printf("FALLO eglMakeCurrent 0x%x\n", eglGetError()); return 3; }
	}
	printf("OK: contexto con la ventana. GL_RENDERER=%s\n", glGetString(GL_RENDERER)); fflush(stdout);
	fflush(stdout);
	log_perform = 0;
	for (fr = 0; fr < 30; fr++) {
		glViewport(0, 0, W, H);
		glClearColor((fr % 10) / 10.0f, 0.2f, 1.0f - (fr % 10) / 10.0f, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		if (!eglSwapBuffers(dpy, surf)) { printf("FALLO eglSwapBuffers frame %d err=0x%x\n", fr, eglGetError()); return 4; }
	}
	glFinish();
	printf("OK: 30 eglSwapBuffers; dequeue=%d queue=%d\n", nd, nq);
	if (nq >= 30 && ahb[0]) {
		int k;
		for (k = 0; k < NBUF; k++) {
			void *p = 0;
			int rc = AHardwareBuffer_lock(ahb[k], AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, 0, &p);
			if (!rc && p) {
				unsigned char *c = p;
				unsigned long nz = 0, tot = (unsigned long)bufs[k]->stride * bufs[k]->height * 4, j;
				for (j = 0; j < tot; j++) if (c[j]) nz++;
				printf("buffer %d: lock rc=%d, bytes distintos de 0: %lu de %lu; px(0,0)=%d %d %d %d; px(centro)=%d %d %d %d\n", k, rc, nz, tot,
				       c[0], c[1], c[2], c[3], c[(H / 2 * bufs[k]->stride + W / 2) * 4], c[(H / 2 * bufs[k]->stride + W / 2) * 4 + 1],
				       c[(H / 2 * bufs[k]->stride + W / 2) * 4 + 2], c[(H / 2 * bufs[k]->stride + W / 2) * 4 + 3]);
				AHardwareBuffer_unlock(ahb[k], 0);
			} else printf("buffer %d: lock rc=%d\n", k, rc);
		}
	}
	(void)i;
	printf("FIN\n");
	return 0;
}
