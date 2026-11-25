/* Prueba de presentacion sin copia: AHardwareBuffer (gralloc de Arm) -> dma-buf -> PRIME_FD_TO_HANDLE -> ADDFB2 -> SETCRTC.
 * uso: ahbdrm [-g] [segundos]   -g = usage solo de GPU (sin CPU), el buffer se rellena con la GPU no con CPU */
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <drm/drm_fourcc.h>

typedef struct { int version, numFds, numInts; int data[0]; } nh_t;
static int fd;
#define U64(p) ((uint64_t)(uintptr_t)(p))
static int dx(unsigned long r, void *a) { int rc; do rc = ioctl(fd, r, a); while (rc < 0 && (errno == EINTR || errno == EAGAIN)); return rc; }

int main(int argc, char **argv)
{
	int gpu_only = 0, secs = 8, i;
	AHardwareBuffer_Desc d, o;
	AHardwareBuffer *b = 0;
	const nh_t *(*gnh)(const AHardwareBuffer *);
	const nh_t *h;
	struct drm_mode_card_res res;
	uint32_t *crtcs, *conns, *encs, conn = 0, crtc = 0, nconn;
	struct drm_mode_modeinfo mode, *modes = 0;
	struct drm_mode_get_plane_res pr;
	struct drm_set_client_cap cc;
	uint32_t *planes;
	off_t sz;

	for (i = 1; i < argc; i++) { if (!strcmp(argv[i], "-g")) gpu_only = 1; else secs = atoi(argv[i]); }
	fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (fd < 0) { printf("FALLO open card0: %s\n", strerror(errno)); return 2; }
	ioctl(fd, DRM_IOCTL_SET_MASTER, 0);

	/* planos y formatos */
	memset(&cc, 0, sizeof cc); cc.capability = DRM_CLIENT_CAP_UNIVERSAL_PLANES; cc.value = 1; ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &cc);
	memset(&pr, 0, sizeof pr);
	dx(DRM_IOCTL_MODE_GETPLANERESOURCES, &pr);
	planes = calloc(pr.count_planes + 1, 4); pr.plane_id_ptr = U64(planes);
	dx(DRM_IOCTL_MODE_GETPLANERESOURCES, &pr);
	for (i = 0; i < (int)pr.count_planes; i++) {
		struct drm_mode_get_plane p; uint32_t *fm; unsigned k;
		memset(&p, 0, sizeof p); p.plane_id = planes[i];
		dx(DRM_IOCTL_MODE_GETPLANE, &p);
		fm = calloc(p.count_format_types + 1, 4); p.format_type_ptr = U64(fm);
		dx(DRM_IOCTL_MODE_GETPLANE, &p);
		printf("plano %u (possible_crtcs=0x%x) formatos:", planes[i], p.possible_crtcs);
		for (k = 0; k < p.count_format_types; k++) printf(" %c%c%c%c", fm[k] & 255, (fm[k] >> 8) & 255, (fm[k] >> 16) & 255, (fm[k] >> 24) & 255);
		printf("\n");
	}

	/* conector, modo, crtc (igual que fakewin/drmshow) */
	memset(&res, 0, sizeof res); dx(DRM_IOCTL_MODE_GETRESOURCES, &res);
	crtcs = calloc(res.count_crtcs, 4); conns = calloc(res.count_connectors, 4); encs = calloc(res.count_encoders, 4);
	nconn = res.count_connectors;
	res.crtc_id_ptr = U64(crtcs); res.connector_id_ptr = U64(conns); res.encoder_id_ptr = U64(encs); res.count_fbs = 0; res.fb_id_ptr = 0;
	dx(DRM_IOCTL_MODE_GETRESOURCES, &res);
	for (i = 0; i < (int)nconn && !conn; i++) {
		struct drm_mode_get_connector c; memset(&c, 0, sizeof c); c.connector_id = conns[i];
		if (dx(DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0 || c.connection != 1 || !c.count_modes) continue;
		modes = calloc(c.count_modes, sizeof *modes); c.modes_ptr = U64(modes);
		c.count_props = 0; c.count_encoders = 0; c.props_ptr = c.prop_values_ptr = c.encoders_ptr = 0;
		if (dx(DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0) continue;
		conn = conns[i];
		if (c.encoder_id) { struct drm_mode_get_encoder e; memset(&e, 0, sizeof e); e.encoder_id = c.encoder_id; if (!dx(DRM_IOCTL_MODE_GETENCODER, &e)) crtc = e.crtc_id; }
		mode = modes[0];
		{ unsigned m; for (m = 0; m < c.count_modes; m++) if (modes[m].vrefresh == 60) { mode = modes[m]; break; } }
	}
	if (!conn) { printf("FALLO: sin conector\n"); return 2; }
	if (!crtc) crtc = crtcs[0];
	printf("panel %ux%u@%u crtc %u conn %u\n", mode.hdisplay, mode.vdisplay, mode.vrefresh, crtc, conn);

	/* AHardwareBuffer a tamano del panel */
	memset(&d, 0, sizeof d);
	d.width = mode.hdisplay; d.height = mode.vdisplay; d.layers = 1; d.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
	d.usage = AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT | AHARDWAREBUFFER_USAGE_COMPOSER_OVERLAY;
	if (!gpu_only) d.usage |= AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN | AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN;
	if (AHardwareBuffer_allocate(&d, &b)) { printf("FALLO AHardwareBuffer_allocate (usage 0x%llx)\n", (unsigned long long)d.usage); return 2; }
	AHardwareBuffer_describe(b, &o);
	printf("AHB: %ux%u stride=%u fmt=%u usage=0x%llx\n", o.width, o.height, o.stride, o.format, (unsigned long long)o.usage);
	gnh = dlsym(RTLD_DEFAULT, "AHardwareBuffer_getNativeHandle");
	if (!gnh) { printf("FALLO: sin AHardwareBuffer_getNativeHandle\n"); return 2; }
	h = gnh(b);
	printf("native_handle: version=%d numFds=%d numInts=%d fds:", h->version, h->numFds, h->numInts);
	for (i = 0; i < h->numFds; i++) printf(" %d", h->data[i]);
	printf("\n  ints:");
	for (i = 0; i < h->numInts; i++) printf(" 0x%x", h->data[h->numFds + i]);
	printf("\n");
	sz = lseek(h->data[0], 0, SEEK_END); lseek(h->data[0], 0, SEEK_SET);
	printf("dma-buf fd %d: tamano %lld; esperado lineal %u (stride*alto*4)%s\n", h->data[0], (long long)sz, o.stride * o.height * 4,
	       sz == (off_t)(o.stride * o.height * 4) ? "  -> LINEAL" : "  -> NO coincide (¿AFBC o relleno?)");

	if (!gpu_only) {
		void *p = 0;
		if (!AHardwareBuffer_lock(b, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, 0, &p) && p) {
			unsigned x, y;
			for (y = 0; y < o.height; y++) {
				unsigned char *r = (unsigned char *)p + (size_t)y * o.stride * 4;
				for (x = 0; x < o.width; x++) {
					unsigned bar = x * 8 / o.width;
					static const unsigned char c[8][3] = { {255,255,255}, {255,255,0}, {0,255,255}, {0,255,0}, {255,0,255}, {255,0,0}, {0,0,255}, {20,20,20} };
					r[x*4] = c[bar][0]; r[x*4+1] = c[bar][1]; r[x*4+2] = c[bar][2]; r[x*4+3] = 255;
					if (y < 8 || y >= o.height - 8 || x < 8 || x >= o.width - 8) { r[x*4] = 255; r[x*4+1] = 255; r[x*4+2] = 255; }
				}
			}
			AHardwareBuffer_unlock(b, 0);
			printf("buffer rellenado por CPU (8 barras RGBA y marco blanco)\n");
		}
	}

	{
		struct drm_prime_handle ph; struct drm_mode_fb_cmd2 f2; struct drm_mode_crtc sc;
		static const uint32_t fmts[] = { DRM_FORMAT_ABGR8888, DRM_FORMAT_XBGR8888, DRM_FORMAT_ARGB8888, DRM_FORMAT_XRGB8888 };
		int ok = 0; uint32_t fb = 0; unsigned k;
		memset(&ph, 0, sizeof ph); ph.fd = h->data[0]; ph.flags = 0;
		if (dx(DRM_IOCTL_PRIME_FD_TO_HANDLE, &ph) < 0) { printf("FALLO PRIME_FD_TO_HANDLE: errno %d (%s)\n", errno, strerror(errno)); return 3; }
		printf("PRIME_FD_TO_HANDLE ok: handle %u\n", ph.handle);
		for (k = 0; k < sizeof fmts / sizeof *fmts && !ok; k++) {
			memset(&f2, 0, sizeof f2);
			f2.width = o.width; f2.height = o.height; f2.pixel_format = fmts[k]; f2.handles[0] = ph.handle; f2.pitches[0] = o.stride * 4;
			if (dx(DRM_IOCTL_MODE_ADDFB2, &f2) == 0) { ok = 1; fb = f2.fb_id; printf("ADDFB2 %c%c%c%c OK: fb %u\n", fmts[k] & 255, (fmts[k] >> 8) & 255, (fmts[k] >> 16) & 255, (fmts[k] >> 24) & 255, fb); }
			else printf("ADDFB2 %c%c%c%c: errno %d (%s)\n", fmts[k] & 255, (fmts[k] >> 8) & 255, (fmts[k] >> 16) & 255, (fmts[k] >> 24) & 255, errno, strerror(errno));
		}
		if (!ok) return 3;
		memset(&sc, 0, sizeof sc); sc.crtc_id = crtc; sc.fb_id = fb; sc.set_connectors_ptr = U64(&conn); sc.count_connectors = 1; sc.mode_valid = 1; sc.mode = mode;
		if (dx(DRM_IOCTL_MODE_SETCRTC, &sc) < 0) { printf("FALLO SETCRTC con el buffer importado: errno %d (%s)\n", errno, strerror(errno)); return 4; }
		printf("SETCRTC ok con el dma-buf de la Mali: mostrando %d s SIN COPIA\n", secs);
		fflush(stdout);
		sleep(secs);
	}
	printf("FIN\n");
	return 0;
}
