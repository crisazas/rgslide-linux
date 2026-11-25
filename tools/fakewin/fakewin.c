/* Ventana falsa: ANativeWindow propio que reparte AHardwareBuffer reales y cuenta los queueBuffer.
 * Se carga el blob con dlopen y se llama a eglCreateWindowSurface con esta ventana. Solo bionic (NDK). */
#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <drm/drm_fourcc.h>
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
static int busy[NBUF], nq = 0, nd = 0, ncall = 0, next_buf = 0;
static int qlog[64], qfence[64], dlog[64], nfence = 0;
static int log_perform = 1;


/* ---------------- salida al panel por DRM (legacy SETCRTC + dumb buffer), igual que tools/drmshow ---------------- */
static int dfd = -1, dpresent = 0;
static uint32_t d_fb, d_crtc, d_conn, d_pitch;
static unsigned char *d_map;
static unsigned d_w, d_h;
static double d_wait_fence = 0, d_copy = 0;
static int d_frames = 0;
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
#define U64(p) ((uint64_t)(uintptr_t)(p))
static int dx(unsigned long r, void *a) { int rc; do rc = ioctl(dfd, r, a); while (rc < 0 && (errno == EINTR || errno == EAGAIN)); return rc; }

static int drm_open(const char *dev)
{
	struct drm_mode_card_res res; uint32_t *crtcs, *conns, *encs, i, nconn;
	struct drm_mode_modeinfo *modes = 0; uint32_t nmodes = 0, enc_id = 0;
	struct drm_mode_create_dumb cd; struct drm_mode_map_dumb md; struct drm_mode_fb_cmd2 f2;
	struct drm_mode_crtc sc; struct drm_mode_modeinfo mode; int pick = -1;
	dfd = open(dev, O_RDWR | O_CLOEXEC);
	if (dfd < 0) { printf("DRM: open %s: %s\n", dev, strerror(errno)); return -1; }
	ioctl(dfd, DRM_IOCTL_SET_MASTER, 0);
	memset(&res, 0, sizeof res);
	if (dx(DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) return -1;
	crtcs = calloc(res.count_crtcs, 4); conns = calloc(res.count_connectors, 4); encs = calloc(res.count_encoders, 4);
	nconn = res.count_connectors;
	res.crtc_id_ptr = U64(crtcs); res.connector_id_ptr = U64(conns); res.encoder_id_ptr = U64(encs); res.count_fbs = 0; res.fb_id_ptr = 0;
	if (dx(DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) return -1;
	for (i = 0; i < nconn && !d_conn; i++) {
		struct drm_mode_get_connector c; memset(&c, 0, sizeof c); c.connector_id = conns[i];
		if (dx(DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0 || c.connection != 1 || !c.count_modes) continue;
		nmodes = c.count_modes; modes = calloc(nmodes, sizeof *modes); c.modes_ptr = U64(modes);
		c.count_props = 0; c.count_encoders = 0; c.props_ptr = c.prop_values_ptr = c.encoders_ptr = 0;
		if (dx(DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0) { free(modes); modes = 0; continue; }
		d_conn = conns[i]; enc_id = c.encoder_id;
	}
	if (!d_conn) { printf("DRM: sin conector conectado\n"); return -1; }
	for (i = 0; i < nmodes; i++) if (modes[i].vrefresh == 60 && pick < 0) pick = (int)i;
	if (pick < 0) pick = 0;
	mode = modes[pick];
	if (enc_id) { struct drm_mode_get_encoder e; memset(&e, 0, sizeof e); e.encoder_id = enc_id; if (dx(DRM_IOCTL_MODE_GETENCODER, &e) == 0) d_crtc = e.crtc_id; }
	if (!d_crtc) d_crtc = crtcs[0];
	memset(&cd, 0, sizeof cd); cd.width = mode.hdisplay; cd.height = mode.vdisplay; cd.bpp = 32;
	if (dx(DRM_IOCTL_MODE_CREATE_DUMB, &cd) < 0) { printf("DRM: CREATE_DUMB: %s\n", strerror(errno)); return -1; }
	memset(&md, 0, sizeof md); md.handle = cd.handle;
	if (dx(DRM_IOCTL_MODE_MAP_DUMB, &md) < 0) return -1;
	d_map = mmap(0, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, dfd, md.offset);
	if (d_map == MAP_FAILED) return -1;
	memset(d_map, 0, cd.size);
	d_w = mode.hdisplay; d_h = mode.vdisplay; d_pitch = cd.pitch;
	memset(&f2, 0, sizeof f2); f2.width = d_w; f2.height = d_h; f2.pixel_format = DRM_FORMAT_XRGB8888; f2.handles[0] = cd.handle; f2.pitches[0] = cd.pitch;
	if (dx(DRM_IOCTL_MODE_ADDFB2, &f2) < 0) { printf("DRM: ADDFB2: %s\n", strerror(errno)); return -1; }
	d_fb = f2.fb_id;
	memset(&sc, 0, sizeof sc); sc.crtc_id = d_crtc; sc.fb_id = d_fb; sc.set_connectors_ptr = U64(&d_conn); sc.count_connectors = 1; sc.mode_valid = 1; sc.mode = mode;
	if (dx(DRM_IOCTL_MODE_SETCRTC, &sc) < 0) { printf("DRM: SETCRTC: %s\n", strerror(errno)); return -1; }
	printf("DRM: panel %ux%u@%u (crtc %u, conector %u, fb %u, pitch %u)\n", d_w, d_h, mode.vrefresh, d_crtc, d_conn, d_fb, d_pitch);
	dpresent = 1;
	return 0;
}

/* copia el buffer de la Mali (RGBA8888) al dumb XRGB8888 (B,G,R,X en memoria) escalando por vecino mas cercano si no coincide */
static void drm_show(wbuf_t *b, int idx, int fence)
{
	void *p = 0; unsigned y, x; double t0 = now_s(), t1, t2;
	if (!dpresent) { if (fence >= 0) close(fence); return; }
	if (fence >= 0) { struct pollfd pf = { .fd = fence, .events = POLLIN }; poll(&pf, 1, 1000); close(fence); }
	t1 = now_s(); d_wait_fence += t1 - t0;
	if (AHardwareBuffer_lock(ahb[idx], AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, 0, &p) || !p) return;
	for (y = 0; y < d_h; y++) {
		unsigned sy = (unsigned)((unsigned long)y * b->height / d_h);
		const unsigned char *s = (const unsigned char *)p + (size_t)sy * b->stride * 4;
		unsigned char *d = d_map + (size_t)y * d_pitch;
		if ((unsigned)b->width == d_w) {
			for (x = 0; x < d_w; x++) { d[x * 4] = s[x * 4 + 2]; d[x * 4 + 1] = s[x * 4 + 1]; d[x * 4 + 2] = s[x * 4]; d[x * 4 + 3] = 0; }
		} else {
			for (x = 0; x < d_w; x++) { unsigned sx = (unsigned)((unsigned long)x * b->width / d_w); d[x * 4] = s[sx * 4 + 2]; d[x * 4 + 1] = s[sx * 4 + 1]; d[x * 4 + 2] = s[sx * 4]; d[x * 4 + 3] = 0; }
		}
	}
	AHardwareBuffer_unlock(ahb[idx], 0);
	t2 = now_s(); d_copy += t2 - t1;
	d_frames++;
}

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
	int i, k;
	(void)w;
	if (!ahb[0] && alloc_all()) return -1;
	for (k = 0; k < NBUF; k++) {
		i = (next_buf + k) % NBUF;
		if (!busy[i]) {
			busy[i] = 1; next_buf = (i + 1) % NBUF; *b = bufs[i]; *fence = -1;
			if (nd < 64) dlog[nd] = i;
			nd++;
			return 0;
		}
	}
	return -1;
}
static int f_queue(fw_t *w, wbuf_t *b, int fence)
{
	int i, idx = -1;
	(void)w;
	for (i = 0; i < NBUF; i++) if (bufs[i] == b) { busy[i] = 0; idx = i; }
	if (nq < 64) { qlog[nq] = idx; qfence[nq] = fence; }
	if (fence >= 0) nfence++;
	nq++;
	if (dpresent && idx >= 0) drm_show(b, idx, fence); else if (fence >= 0) close(fence);
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

	int show = 0, show_secs = 10, ai;
	for (ai = 1; ai < argc; ai++) {
		if (!strcmp(argv[ai], "-d")) { show = 1; if (ai + 1 < argc && argv[ai + 1][0] != '-') show_secs = atoi(argv[++ai]); }
		else if (ai + 1 < argc && argv[ai + 1][0] != '-' && atoi(argv[ai]) > 0) { W = atoi(argv[ai]); H = atoi(argv[ai + 1]); ai++; }
	}
	if (show) { W = 1280; H = 960; }
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
	LD(glEnable, void, (GLenum));
	LD(glScissor, void, (GLint, GLint, GLsizei, GLsizei));
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
	if (show) {
		double t0, tn, dt; int nf = 0; unsigned char px[4] = { 0 };
		if (drm_open("/dev/dri/card0")) { printf("FALLO: no pude abrir el panel\n"); return 5; }
		fflush(stdout);
		glEnable(GL_SCISSOR_TEST);
		t0 = now_s();
		while ((tn = now_s()) - t0 < show_secs) {
			float ph = (float)(tn - t0);
			int bw = 160, bh = 160, x = (int)((W - bw) * (0.5f + 0.5f * __builtin_sinf(ph * 1.7f))), y = (int)((H - bh) * (0.5f + 0.5f * __builtin_sinf(ph * 2.3f)));
			glViewport(0, 0, W, H);
			glScissor(0, 0, W, H);
			glClearColor(0.05f, 0.05f, 0.25f, 1); glClear(GL_COLOR_BUFFER_BIT);
			glScissor(0, H - 24, (int)(W * ph / show_secs), 24); glClearColor(0.1f, 0.9f, 0.2f, 1); glClear(GL_COLOR_BUFFER_BIT);
			glScissor(x, y, bw, bh); glClearColor(1.0f, 0.5f + 0.5f * __builtin_sinf(ph * 5.0f), 0.1f, 1); glClear(GL_COLOR_BUFFER_BIT);
			glScissor(0, 0, W, H);
			if (!eglSwapBuffers(dpy, surf)) { printf("FALLO eglSwapBuffers err=0x%x\n", eglGetError()); return 4; }
			nf++;
		}
		dt = now_s() - t0;
		glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		printf("PRESENTADO: %d frames en %.2f s = %.1f fps (queue=%d dequeue=%d, mostrados=%d)\n", nf, dt, nf / dt, nq, nd, d_frames);
		printf("  tiempo medio por frame: esperar fence %.2f ms, copiar+convertir %.2f ms\n", 1000 * d_wait_fence / (d_frames ? d_frames : 1), 1000 * d_copy / (d_frames ? d_frames : 1));
		printf("FIN\n");
		return 0;
	}
	fflush(stdout);
	log_perform = 0;
	{
		int bad = 0, k, seen[NBUF] = { 0 };
		for (fr = 0; fr < 30; fr++) {
			float r = (fr % 10) / 10.0f, bl = 1.0f - (fr % 10) / 10.0f;
			int q0 = nq, er, eb;
			glViewport(0, 0, W, H);
			glClearColor(r, 0.2f, bl, 1);
			glClear(GL_COLOR_BUFFER_BIT);
			if (!eglSwapBuffers(dpy, surf)) { printf("FALLO eglSwapBuffers frame %d err=0x%x\n", fr, eglGetError()); return 4; }
			glFinish();
			k = qlog[q0 < 64 ? q0 : 63];
			er = (int)(r * 255.0f + 0.5f); eb = (int)(bl * 255.0f + 0.5f);
			if (nq > q0 && k >= 0) {
				void *p = 0;
				if (!AHardwareBuffer_lock(ahb[k], AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, 0, &p) && p) {
					unsigned char *c = p + ((size_t)(H / 2) * bufs[k]->stride + W / 2) * 4;
					int ok = abs(c[0] - er) <= 1 && abs(c[1] - 51) <= 1 && abs(c[2] - eb) <= 1 && c[3] == 255;
					seen[k]++;
					if (!ok) { bad++; if (bad <= 6) printf("frame %2d buffer %d MAL: leido %d %d %d %d, esperado %d 51 %d 255\n", fr, k, c[0], c[1], c[2], c[3], er, eb); }
					AHardwareBuffer_unlock(ahb[k], 0);
				}
			} else { bad++; if (bad <= 6) printf("frame %2d: no hubo queueBuffer (nq=%d)\n", fr, nq); }
		}
		printf("RESULTADO: %d/30 fotogramas correctos; dequeue=%d queue=%d fences_en_queue=%d; usos por buffer: %d %d %d\n", 30 - bad, nd, nq, nfence, seen[0], seen[1], seen[2]);
		printf("orden de queue (buffer): ");
		for (k = 0; k < 12 && k < nq; k++) printf("%d ", qlog[k]);
		printf("...\n");
	}
	(void)i;
	printf("FIN\n");
	return 0;
}
