/* refresh-flip: fija un modo de DSI-1 (1280x960@HZ), hace page-flips entre dos dumb buffers y mide el intervalo REAL de los
 * eventos de flip (reloj propio: los timestamps del evento vienen a 0 en este kernel). Restaura el crtc original al salir (y con SIGINT/SIGTERM o alarm).
 * Base: SETCRTC legacy a 60 Hz. Si HZ != 60: commit ATOMICO con MODE_ID del modo pedido y la propiedad CRTC "frame rate change"=1
 * (sin ella sprd_dpu/dpu_modeset no reprograma el DPU y el panel sigue a 60 Hz). Con el 3er argumento "legacy" cambia con SETCRTC (no funciona).
 * Al salir vuelve a 60 Hz por el mismo camino atomico.
 * Uso: refresh-flip HZ [segundos] [legacy]   Compilar: aarch64-linux-gnu-gcc -O2 -static -o out/refresh-flip scripts/refresh-flip.c */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <drm/drm_fourcc.h>
#define U64(p) ((uint64_t)(uintptr_t)(p))
static int fd;
static volatile sig_atomic_t stop;
static void on_sig(int s) { (void)s; stop = 1; }
static int x(unsigned long r, void *a) { int e; do e = ioctl(fd, r, a); while (e < 0 && (errno == EINTR || errno == EAGAIN)); return e; }

static int find_prop(uint32_t obj, uint32_t type, const char *name, uint32_t *id)
{
	struct drm_mode_obj_get_properties g = { .obj_id = obj, .obj_type = type };
	if (x(DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &g) < 0) return -1;
	uint32_t *ids = calloc(g.count_props + 1, 4); uint64_t *vals = calloc(g.count_props + 1, 8);
	g.props_ptr = U64(ids); g.prop_values_ptr = U64(vals);
	if (x(DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &g) < 0) return -1;
	for (uint32_t i = 0; i < g.count_props; i++) {
		struct drm_mode_get_property gp = { .prop_id = ids[i] };
		if (x(DRM_IOCTL_MODE_GETPROPERTY, &gp) == 0 && !strcmp(gp.name, name)) { *id = ids[i]; free(ids); free(vals); return 0; }
	}
	free(ids); free(vals);
	return -1;
}

/* cambia el modo del crtc por commit atomico con "frame rate change"=1 */
static int atomic_mode(uint32_t conn, uint32_t crtc, struct drm_mode_modeinfo *m)
{
	uint32_t p_crtc_id, p_mode, p_active, p_frc;
	struct drm_mode_create_blob cb = { .data = U64(m), .length = sizeof *m };
	if (find_prop(conn, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID", &p_crtc_id) || find_prop(crtc, DRM_MODE_OBJECT_CRTC, "MODE_ID", &p_mode) ||
	    find_prop(crtc, DRM_MODE_OBJECT_CRTC, "ACTIVE", &p_active) || find_prop(crtc, DRM_MODE_OBJECT_CRTC, "frame rate change", &p_frc)) {
		fprintf(stderr, "faltan propiedades atomicas (CRTC_ID/MODE_ID/ACTIVE/frame rate change)\n"); return -1;
	}
	if (x(DRM_IOCTL_MODE_CREATEPROPBLOB, &cb) < 0) { perror("createblob"); return -1; }
	uint32_t objs[2] = { conn, crtc }, counts[2] = { 1, 3 };
	uint32_t props[4] = { p_crtc_id, p_mode, p_active, p_frc };
	uint64_t vals[4] = { crtc, cb.blob_id, 1, 1 };
	struct drm_mode_atomic at = { .flags = DRM_MODE_ATOMIC_ALLOW_MODESET, .count_objs = 2, .objs_ptr = U64(objs),
		.count_props_ptr = U64(counts), .props_ptr = U64(props), .prop_values_ptr = U64(vals) };
	int r = x(DRM_IOCTL_MODE_ATOMIC, &at);
	if (r < 0) fprintf(stderr, "ATOMIC: %s\n", strerror(errno));
	struct drm_mode_destroy_blob db = { .blob_id = cb.blob_id }; x(DRM_IOCTL_MODE_DESTROYPROPBLOB, &db);
	return r;
}

int main(int argc, char **argv)
{
	unsigned hz = argc > 1 ? atoi(argv[1]) : 60, secs = argc > 2 ? atoi(argv[2]) : 3, i;
	struct drm_mode_card_res res = {0};
	struct drm_mode_modeinfo *modes = NULL, *mode = NULL, *m60 = NULL;
	int legacy = argc > 3 && !strcmp(argv[3], "legacy"), switched = 0;
	struct drm_mode_get_connector c = {0};
	uint32_t conn = 0, crtc, fbs[2];
	void *map[2]; uint32_t pitch = 0;
	signal(SIGINT, on_sig); signal(SIGTERM, on_sig); signal(SIGALRM, on_sig); alarm(secs + 4);
	fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (fd < 0) { perror("open"); return 1; }
	if (ioctl(fd, DRM_IOCTL_SET_MASTER, 0) < 0) fprintf(stderr, "SET_MASTER: %s\n", strerror(errno));
	x(DRM_IOCTL_MODE_GETRESOURCES, &res);
	uint32_t *cs = calloc(res.count_crtcs + 1, 4), *cn = calloc(res.count_connectors + 1, 4), *en = calloc(res.count_encoders + 1, 4);
	res.crtc_id_ptr = U64(cs); res.connector_id_ptr = U64(cn); res.encoder_id_ptr = U64(en); res.count_fbs = 0; res.fb_id_ptr = 0;
	x(DRM_IOCTL_MODE_GETRESOURCES, &res);
	for (i = 0; i < res.count_connectors && !conn; i++) {
		memset(&c, 0, sizeof c); c.connector_id = cn[i];
		if (x(DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0 || c.connection != 1 || !c.count_modes) continue;
		modes = calloc(c.count_modes, sizeof *modes);
		c.modes_ptr = U64(modes); c.count_props = c.count_encoders = 0; c.props_ptr = c.prop_values_ptr = c.encoders_ptr = 0;
		if (x(DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0) continue;
		conn = cn[i];
	}
	if (!conn) { fprintf(stderr, "sin conector\n"); return 1; }
	for (i = 0; i < c.count_modes; i++) if (modes[i].vrefresh == hz) { mode = &modes[i]; break; }
	if (!mode) { fprintf(stderr, "no hay modo @%u\n", hz); return 1; }
	for (i = 0; i < c.count_modes; i++) if (modes[i].vrefresh == 60) { m60 = &modes[i]; break; }
	if (!m60) { fprintf(stderr, "no hay modo @60\n"); return 1; }
	struct drm_mode_get_encoder e = { .encoder_id = c.encoder_id };
	x(DRM_IOCTL_MODE_GETENCODER, &e); crtc = e.crtc_id ? e.crtc_id : cs[0];
	struct drm_mode_crtc saved = { .crtc_id = crtc };
	x(DRM_IOCTL_MODE_GETCRTC, &saved);
	for (i = 0; i < 2; i++) {
		struct drm_mode_create_dumb cd = { .width = mode->hdisplay, .height = mode->vdisplay, .bpp = 32 };
		if (x(DRM_IOCTL_MODE_CREATE_DUMB, &cd) < 0) { perror("create_dumb"); return 1; }
		struct drm_mode_map_dumb md = { .handle = cd.handle };
		x(DRM_IOCTL_MODE_MAP_DUMB, &md);
		map[i] = mmap(NULL, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, md.offset);
		pitch = cd.pitch;
		struct drm_mode_fb_cmd2 f2 = { .width = mode->hdisplay, .height = mode->vdisplay, .pixel_format = DRM_FORMAT_XRGB8888 };
		f2.handles[0] = cd.handle; f2.pitches[0] = cd.pitch;
		if (x(DRM_IOCTL_MODE_ADDFB2, &f2) < 0) { perror("addfb2"); return 1; }
		fbs[i] = f2.fb_id;
		for (uint32_t y = 0; y < mode->vdisplay; y++) {
			uint32_t *row = (uint32_t *)((char *)map[i] + (size_t)y * pitch);
			for (uint32_t xx = 0; xx < mode->hdisplay; xx++)
				row[xx] = ((xx / 64 + y / 64) & 1) ? (i ? 0x2060ff : 0xffffff) : (i ? 0x000000 : 0x2060ff);
		}
	}
	struct drm_mode_crtc sc = { .crtc_id = crtc, .fb_id = fbs[0], .set_connectors_ptr = U64(&conn), .count_connectors = 1, .mode_valid = 1, .mode = *(legacy ? mode : m60) };
	if (x(DRM_IOCTL_MODE_SETCRTC, &sc) < 0) { fprintf(stderr, "SETCRTC: %s\n", strerror(errno)); return 1; }
	if (!legacy && hz != 60) {
		struct drm_set_client_cap cc = { .capability = DRM_CLIENT_CAP_ATOMIC, .value = 1 };
		struct drm_set_client_cap up = { .capability = DRM_CLIENT_CAP_UNIVERSAL_PLANES, .value = 1 };
		ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &up);
		if (ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &cc) < 0) fprintf(stderr, "CLIENT_CAP_ATOMIC: %s\n", strerror(errno));
		else if (atomic_mode(conn, crtc, mode) == 0) switched = 1;
	}
	printf("modo %s %u Hz pedido (%s, vtotal %u clock %u)\n", mode->name, mode->vrefresh, legacy ? "legacy" : hz == 60 ? "base" : switched ? "atomico OK" : "atomico FALLO", mode->vtotal, mode->clock);
	fflush(stdout);
	double first = -1, last = -1, sum = 0, mn = 1e9, mx = 0; unsigned n = 0, k = 0;
	double tend = 0;
	struct pollfd pf = { .fd = fd, .events = POLLIN };
	struct drm_mode_crtc_page_flip pf0 = { .crtc_id = crtc, .fb_id = fbs[1], .flags = DRM_MODE_PAGE_FLIP_EVENT };
	if (x(DRM_IOCTL_MODE_PAGE_FLIP, &pf0) < 0) fprintf(stderr, "PAGE_FLIP: %s\n", strerror(errno));
	while (!stop) {
		if (poll(&pf, 1, 500) <= 0) break;
		char ev[256]; int r = read(fd, ev, sizeof ev);
		for (int o = 0; r > 0 && o + 8 <= r;) {
			struct drm_event_vblank *v = (struct drm_event_vblank *)(ev + o);
			if (v->base.type == DRM_EVENT_FLIP_COMPLETE) {
				struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
				double t = ts.tv_sec + ts.tv_nsec / 1e9;
				if (first < 0) { first = t; tend = t + secs; }
				else { double d = (t - last) * 1000; if (n >= 3) { sum += d; if (d < mn) mn = d; if (d > mx) mx = d; } n++; }
				last = t; k++;
				struct drm_mode_crtc_page_flip p = { .crtc_id = crtc, .fb_id = fbs[k & 1], .flags = DRM_MODE_PAGE_FLIP_EVENT };
				if (t < tend) x(DRM_IOCTL_MODE_PAGE_FLIP, &p); else stop = 1;
			}
			if (v->base.length < 8) break;
			o += v->base.length;
		}
	}
	if (n > 4) printf("flips %u en %.2f s: intervalo medio %.3f ms (min %.3f max %.3f) = %.1f Hz\n", n, last - first, sum / (n - 3), mn, mx, 1000.0 / (sum / (n - 3)));
	else printf("pocos flips (%u): el modo no entrega eventos\n", n);
	if (switched && atomic_mode(conn, crtc, m60) == 0) printf("vuelta atomica a 60 Hz\n");
	else if (switched) fprintf(stderr, "AVISO: la vuelta a 60 Hz fallo\n");
	struct drm_mode_crtc rc = saved; uint32_t sconn = conn;
	rc.set_connectors_ptr = U64(&sconn); rc.count_connectors = saved.mode_valid ? 1 : 0;
	if (x(DRM_IOCTL_MODE_SETCRTC, &rc) < 0) fprintf(stderr, "restaurar: %s\n", strerror(errno)); else printf("crtc restaurado\n");
	ioctl(fd, DRM_IOCTL_DROP_MASTER, 0);
	return 0;
}
