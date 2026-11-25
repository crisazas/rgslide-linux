/* drmshow: muestra un patron o un PPM en /dev/dri/card0 sin libdrm (solo ioctls DRM).
 * Uso: drmshow [-d dev] [-m WxH[@R]] [-t seg] (-p | fichero.ppm) */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
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

#define CONN_CONNECTED 1
#define U64(p) ((uint64_t)(uintptr_t)(p))
#define LOG(...) do { printf(__VA_ARGS__); fflush(stdout); } while (0)
#define ERR(...) do { fprintf(stderr, __VA_ARGS__); fflush(stderr); } while (0)

static int fd = -1;
static volatile sig_atomic_t stop;

static void on_sig(int s) { (void)s; stop = 1; }

static int xioctl(unsigned long req, void *arg)
{
	int r;
	do {
		r = ioctl(fd, req, arg);
	} while (r == -1 && (errno == EINTR || errno == EAGAIN));
	return r;
}

static void *xcalloc(size_t n, size_t sz)
{
	void *p = calloc(n ? n : 1, sz);
	if (!p) { ERR("sin memoria\n"); exit(1); }
	return p;
}

/* ---- propiedades (solo para el camino atomico) ---- */
static int find_prop(uint32_t obj, uint32_t type, const char *name, uint32_t *id, uint64_t *val)
{
	struct drm_mode_obj_get_properties g = { .obj_id = obj, .obj_type = type };
	uint32_t *ids; uint64_t *vals;
	int ret = -1;

	if (xioctl(DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &g) < 0)
		return -1;
	ids = xcalloc(g.count_props, sizeof(*ids));
	vals = xcalloc(g.count_props, sizeof(*vals));
	g.props_ptr = U64(ids);
	g.prop_values_ptr = U64(vals);
	if (xioctl(DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &g) < 0)
		goto out;
	for (uint32_t i = 0; i < g.count_props; i++) {
		struct drm_mode_get_property p = { .prop_id = ids[i] };
		if (xioctl(DRM_IOCTL_MODE_GETPROPERTY, &p) < 0)
			continue;
		if (!strcmp(p.name, name)) {
			*id = ids[i];
			if (val) *val = vals[i];
			ret = 0;
			break;
		}
	}
out:
	free(ids); free(vals);
	return ret;
}

/* ---- imagen ---- */
static uint32_t rgb(unsigned r, unsigned g, unsigned b) { return (r << 16) | (g << 8) | b; }

static void fill_pattern(uint32_t *px, unsigned w, unsigned h, unsigned stride)
{
	static const uint32_t bars[8] = {
		0xffffff, 0xffff00, 0x00ffff, 0x00ff00, 0xff00ff, 0xff0000, 0x0000ff, 0x000000 };
	unsigned split = h * 2 / 3;

	for (unsigned y = 0; y < h; y++) {
		uint32_t *row = px + (size_t)y * stride;
		for (unsigned x = 0; x < w; x++) {
			uint32_t c;
			if (y < split)
				c = bars[x * 8 / w];
			else {
				unsigned v = x * 255 / (w > 1 ? w - 1 : 1);
				unsigned band = (y - split) * 3 / (h - split ? h - split : 1);
				c = band == 0 ? rgb(v, v, v) : band == 1 ? rgb(v, 0, 255 - v) : rgb(255 - v, v, 0);
			}
			if (x < 4 || y < 4 || x >= w - 4 || y >= h - 4)
				c = 0xffffff;
			row[x] = c;
		}
	}
}

static int ppm_token(FILE *f, unsigned *out)
{
	int c;
	unsigned v = 0;
	for (;;) {
		c = fgetc(f);
		if (c == '#') { while (c != '\n' && c != EOF) c = fgetc(f); }
		else if (c == EOF) return -1;
		else if (c > ' ') break;
	}
	if (c < '0' || c > '9') return -1;
	do { v = v * 10 + (c - '0'); c = fgetc(f); } while (c >= '0' && c <= '9');
	*out = v;
	return 0;
}

static int fill_ppm(const char *path, uint32_t *px, unsigned w, unsigned h, unsigned stride)
{
	FILE *f = fopen(path, "rb");
	unsigned iw, ih, maxv;
	uint8_t *src;

	if (!f) { ERR("no puedo abrir %s: %s\n", path, strerror(errno)); return -1; }
	if (fgetc(f) != 'P' || fgetc(f) != '6' ||
	    ppm_token(f, &iw) || ppm_token(f, &ih) || ppm_token(f, &maxv) ||
	    !iw || !ih || maxv == 0 || maxv > 255) {
		ERR("%s: no es un P6 de 8 bits valido\n", path);
		fclose(f);
		return -1;
	}
	src = xcalloc((size_t)iw * ih, 3);
	if (fread(src, 3, (size_t)iw * ih, f) != (size_t)iw * ih) {
		ERR("%s: datos truncados\n", path);
		free(src); fclose(f);
		return -1;
	}
	fclose(f);
	LOG("ppm %ux%u -> modo %ux%u (vecino mas cercano)\n", iw, ih, w, h);
	for (unsigned y = 0; y < h; y++) {
		const uint8_t *srow = src + (size_t)(y * (uint64_t)ih / h) * iw * 3;
		uint32_t *row = px + (size_t)y * stride;
		for (unsigned x = 0; x < w; x++) {
			const uint8_t *p = srow + (size_t)(x * (uint64_t)iw / w) * 3;
			unsigned r = p[0], g = p[1], b = p[2];
			if (maxv != 255) { r = r * 255 / maxv; g = g * 255 / maxv; b = b * 255 / maxv; }
			row[x] = rgb(r, g, b);
		}
	}
	free(src);
	return 0;
}

static void usage(void)
{
	ERR("uso: drmshow [-d /dev/dri/card0] [-m WxH[@R]] [-t segundos] (-p | fichero.ppm)\n");
}

int main(int argc, char **argv)
{
	const char *dev = "/dev/dri/card0", *ppm = NULL, *want = NULL;
	int pattern = 0, secs = 10, opt;
	unsigned ww = 0, wh = 0, wr = 0;

	while ((opt = getopt(argc, argv, "d:m:t:ph")) != -1) {
		switch (opt) {
		case 'd': dev = optarg; break;
		case 'm': want = optarg; break;
		case 't': secs = atoi(optarg); break;
		case 'p': pattern = 1; break;
		default: usage(); return 2;
		}
	}
	if (optind < argc) ppm = argv[optind];
	if (!pattern == !ppm) { usage(); return 2; }
	if (want && sscanf(want, "%ux%u@%u", &ww, &wh, &wr) < 2) { ERR("-m: formato WxH[@R]\n"); return 2; }

	signal(SIGINT, on_sig);
	signal(SIGTERM, on_sig);

	fd = open(dev, O_RDWR | O_CLOEXEC);
	if (fd < 0) { ERR("open %s: %s\n", dev, strerror(errno)); return 1; }
	LOG("abierto %s\n", dev);

	if (ioctl(fd, DRM_IOCTL_SET_MASTER, 0) < 0)
		LOG("SET_MASTER: %s (se sigue; puede que ya seamos master)\n", strerror(errno));
	else
		LOG("SET_MASTER ok\n");

	struct drm_get_cap cap = { .capability = DRM_CAP_DUMB_BUFFER };
	if (xioctl(DRM_IOCTL_GET_CAP, &cap) == 0)
		LOG("DRM_CAP_DUMB_BUFFER = %llu\n", (unsigned long long)cap.value);

	/* recursos */
	struct drm_mode_card_res res = {0};
	if (xioctl(DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) { ERR("GETRESOURCES: %s\n", strerror(errno)); return 1; }
	uint32_t *crtcs = xcalloc(res.count_crtcs, 4), *conns = xcalloc(res.count_connectors, 4),
		 *encs = xcalloc(res.count_encoders, 4);
	uint32_t ncrtc = res.count_crtcs, nconn = res.count_connectors, nenc = res.count_encoders;
	res.crtc_id_ptr = U64(crtcs);
	res.connector_id_ptr = U64(conns);
	res.encoder_id_ptr = U64(encs);
	res.fb_id_ptr = 0; res.count_fbs = 0;
	if (xioctl(DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) { ERR("GETRESOURCES(2): %s\n", strerror(errno)); return 1; }
	if (res.count_crtcs < ncrtc) ncrtc = res.count_crtcs;
	if (res.count_connectors < nconn) nconn = res.count_connectors;
	if (res.count_encoders < nenc) nenc = res.count_encoders;
	LOG("recursos: %u crtc, %u conectores, %u encoders\n", ncrtc, nconn, nenc);

	/* conector */
	struct drm_mode_modeinfo *modes = NULL;
	uint32_t nmodes = 0, conn_id = 0, enc_id = 0;
	for (uint32_t i = 0; i < nconn && !conn_id; i++) {
		struct drm_mode_get_connector c = { .connector_id = conns[i] };
		if (xioctl(DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0) { ERR("GETCONNECTOR %u: %s\n", conns[i], strerror(errno)); continue; }
		LOG("conector %u: tipo %u conexion %u modos %u\n", conns[i], c.connector_type, c.connection, c.count_modes);
		if (c.connection != CONN_CONNECTED || !c.count_modes)
			continue;
		for (int tries = 0; tries < 4; tries++) {
			free(modes);
			nmodes = c.count_modes;
			modes = xcalloc(nmodes, sizeof(*modes));
			c.modes_ptr = U64(modes);
			c.count_props = 0; c.count_encoders = 0;
			c.props_ptr = c.prop_values_ptr = c.encoders_ptr = 0;
			if (xioctl(DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0) { ERR("GETCONNECTOR(2): %s\n", strerror(errno)); nmodes = 0; break; }
			if (c.count_modes <= nmodes) { nmodes = c.count_modes; break; }
		}
		if (!nmodes) continue;
		conn_id = conns[i];
		enc_id = c.encoder_id;
	}
	if (!conn_id) { ERR("ningun conector CONNECTED con modos\n"); return 1; }
	LOG("conector elegido: %u (encoder actual %u)\n", conn_id, enc_id);
	for (uint32_t i = 0; i < nmodes; i++)
		LOG("  modo %u: %s %ux%u@%u clock %u%s\n", i, modes[i].name, modes[i].hdisplay,
		    modes[i].vdisplay, modes[i].vrefresh, modes[i].clock,
		    (modes[i].type & DRM_MODE_TYPE_PREFERRED) ? " [preferido]" : "");

	/* modo */
	struct drm_mode_modeinfo *mode = NULL;
	if (want) {
		for (uint32_t i = 0; i < nmodes; i++)
			if (modes[i].hdisplay == ww && modes[i].vdisplay == wh && (!wr || modes[i].vrefresh == wr)) {
				mode = &modes[i];
				break;
			}
		if (!mode) { ERR("no hay modo %s\n", want); return 1; }
	} else {
		for (uint32_t i = 0; i < nmodes; i++)
			if ((modes[i].type & DRM_MODE_TYPE_PREFERRED) && (!mode || modes[i].vrefresh > mode->vrefresh))
				mode = &modes[i];
		if (!mode) mode = &modes[0];
	}
	LOG("modo elegido: %s %ux%u@%u\n", mode->name, mode->hdisplay, mode->vdisplay, mode->vrefresh);

	/* crtc */
	uint32_t crtc_id = 0;
	if (enc_id) {
		struct drm_mode_get_encoder e = { .encoder_id = enc_id };
		if (xioctl(DRM_IOCTL_MODE_GETENCODER, &e) == 0) crtc_id = e.crtc_id;
	}
	if (!crtc_id) {
		for (uint32_t i = 0; i < nenc && !crtc_id; i++) {
			struct drm_mode_get_encoder e = { .encoder_id = encs[i] };
			if (xioctl(DRM_IOCTL_MODE_GETENCODER, &e) < 0) continue;
			for (uint32_t k = 0; k < ncrtc; k++)
				if (e.possible_crtcs & (1u << k)) { crtc_id = crtcs[k]; break; }
		}
	}
	if (!crtc_id) { ERR("no encuentro crtc\n"); return 1; }
	uint32_t crtc_idx = 0;
	for (uint32_t k = 0; k < ncrtc; k++) if (crtcs[k] == crtc_id) crtc_idx = k;
	LOG("crtc: %u (indice %u)\n", crtc_id, crtc_idx);

	struct drm_mode_crtc saved = { .crtc_id = crtc_id };
	if (xioctl(DRM_IOCTL_MODE_GETCRTC, &saved) < 0) { ERR("GETCRTC: %s\n", strerror(errno)); return 1; }
	LOG("crtc original: fb %u mode_valid %u\n", saved.fb_id, saved.mode_valid);

	/* dumb buffer */
	struct drm_mode_create_dumb cd = { .width = mode->hdisplay, .height = mode->vdisplay, .bpp = 32 };
	if (xioctl(DRM_IOCTL_MODE_CREATE_DUMB, &cd) < 0) {
		int e = errno;
		ERR("CREATE_DUMB fallo: errno %d (%s)\n", e, strerror(e));
		return 3;
	}
	LOG("dumb: handle %u pitch %u size %llu\n", cd.handle, cd.pitch, (unsigned long long)cd.size);

	struct drm_mode_map_dumb md = { .handle = cd.handle };
	if (xioctl(DRM_IOCTL_MODE_MAP_DUMB, &md) < 0) { ERR("MAP_DUMB: %s\n", strerror(errno)); return 1; }
	void *map = mmap(NULL, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, md.offset);
	if (map == MAP_FAILED) { ERR("mmap: %s\n", strerror(errno)); return 1; }
	LOG("mmap ok\n");

	unsigned stride = cd.pitch / 4;
	memset(map, 0, cd.size);
	if (pattern) fill_pattern(map, mode->hdisplay, mode->vdisplay, stride);
	else if (fill_ppm(ppm, map, mode->hdisplay, mode->vdisplay, stride)) return 1;
	LOG("imagen escrita\n");

	/* framebuffer */
	uint32_t fb_id = 0;
	struct drm_mode_fb_cmd2 f2 = { .width = mode->hdisplay, .height = mode->vdisplay,
		.pixel_format = DRM_FORMAT_XRGB8888 };
	f2.handles[0] = cd.handle; f2.pitches[0] = cd.pitch;
	if (xioctl(DRM_IOCTL_MODE_ADDFB2, &f2) == 0) {
		fb_id = f2.fb_id;
		LOG("ADDFB2 ok: fb %u\n", fb_id);
	} else {
		LOG("ADDFB2: %s; pruebo ADDFB\n", strerror(errno));
		struct drm_mode_fb_cmd f1 = { .width = mode->hdisplay, .height = mode->vdisplay,
			.pitch = cd.pitch, .bpp = 32, .depth = 24, .handle = cd.handle };
		if (xioctl(DRM_IOCTL_MODE_ADDFB, &f1) < 0) { ERR("ADDFB: %s\n", strerror(errno)); return 1; }
		fb_id = f1.fb_id;
		LOG("ADDFB ok: fb %u\n", fb_id);
	}

	/* SETCRTC legacy */
	struct drm_mode_crtc sc = { .crtc_id = crtc_id, .fb_id = fb_id, .set_connectors_ptr = U64(&conn_id),
		.count_connectors = 1, .mode_valid = 1, .mode = *mode };
	int atomic = 0;
	uint32_t blob_id = 0;
	uint32_t p_conn_crtc = 0, p_crtc_mode = 0, p_crtc_active = 0, plane_id = 0;
	uint32_t pl_fb = 0, pl_crtc = 0, pl_sx = 0, pl_sy = 0, pl_sw = 0, pl_sh = 0, pl_cx = 0, pl_cy = 0, pl_cw = 0, pl_ch = 0;

	if (xioctl(DRM_IOCTL_MODE_SETCRTC, &sc) == 0) {
		LOG("SETCRTC ok (legacy)\n");
	} else {
		int e = errno;
		ERR("SETCRTC fallo: errno %d (%s)\n", e, strerror(e));
		if (e != ENOSYS && e != EINVAL && e != EOPNOTSUPP) return 1;
		LOG("pruebo DRM_IOCTL_MODE_ATOMIC\n");
		struct drm_set_client_cap cc = { .capability = DRM_CLIENT_CAP_UNIVERSAL_PLANES, .value = 1 };
		if (ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &cc) < 0) ERR("UNIVERSAL_PLANES: %s\n", strerror(errno));
		cc.capability = DRM_CLIENT_CAP_ATOMIC;
		if (ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &cc) < 0) { ERR("CLIENT_CAP_ATOMIC: %s\n", strerror(errno)); return 1; }

		struct drm_mode_get_plane_res pr = {0};
		if (xioctl(DRM_IOCTL_MODE_GETPLANERESOURCES, &pr) < 0) { ERR("GETPLANERESOURCES: %s\n", strerror(errno)); return 1; }
		uint32_t npl = pr.count_planes;
		uint32_t *pls = xcalloc(npl, 4);
		pr.plane_id_ptr = U64(pls);
		if (xioctl(DRM_IOCTL_MODE_GETPLANERESOURCES, &pr) < 0) { ERR("GETPLANERESOURCES(2): %s\n", strerror(errno)); return 1; }
		if (pr.count_planes < npl) npl = pr.count_planes;
		for (uint32_t i = 0; i < npl && !plane_id; i++) {
			struct drm_mode_get_plane gp = { .plane_id = pls[i] };
			uint32_t tid; uint64_t tv;
			if (xioctl(DRM_IOCTL_MODE_GETPLANE, &gp) < 0 || !(gp.possible_crtcs & (1u << crtc_idx))) continue;
			if (find_prop(pls[i], DRM_MODE_OBJECT_PLANE, "type", &tid, &tv) == 0 && tv == 1)
				plane_id = pls[i];
		}
		if (!plane_id) { ERR("no hay plano primario para el crtc\n"); return 1; }
		LOG("plano primario: %u\n", plane_id);

		if (find_prop(conn_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID", &p_conn_crtc, NULL) ||
		    find_prop(crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID", &p_crtc_mode, NULL) ||
		    find_prop(crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE", &p_crtc_active, NULL) ||
		    find_prop(plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID", &pl_fb, NULL) ||
		    find_prop(plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID", &pl_crtc, NULL) ||
		    find_prop(plane_id, DRM_MODE_OBJECT_PLANE, "SRC_X", &pl_sx, NULL) ||
		    find_prop(plane_id, DRM_MODE_OBJECT_PLANE, "SRC_Y", &pl_sy, NULL) ||
		    find_prop(plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W", &pl_sw, NULL) ||
		    find_prop(plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H", &pl_sh, NULL) ||
		    find_prop(plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_X", &pl_cx, NULL) ||
		    find_prop(plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_Y", &pl_cy, NULL) ||
		    find_prop(plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_W", &pl_cw, NULL) ||
		    find_prop(plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_H", &pl_ch, NULL)) {
			ERR("faltan propiedades atomicas\n");
			return 1;
		}
		struct drm_mode_create_blob cb = { .data = U64(mode), .length = sizeof(*mode) };
		if (xioctl(DRM_IOCTL_MODE_CREATEPROPBLOB, &cb) < 0) { ERR("CREATEPROPBLOB: %s\n", strerror(errno)); return 1; }
		blob_id = cb.blob_id;

		uint32_t objs[3] = { conn_id, crtc_id, plane_id };
		uint32_t counts[3] = { 1, 2, 10 };
		uint32_t props[13] = { p_conn_crtc, p_crtc_mode, p_crtc_active, pl_fb, pl_crtc, pl_sx, pl_sy, pl_sw, pl_sh,
				       pl_cx, pl_cy, pl_cw, pl_ch };
		uint64_t vals[13] = { crtc_id, blob_id, 1, fb_id, crtc_id, 0, 0,
				      (uint64_t)mode->hdisplay << 16, (uint64_t)mode->vdisplay << 16,
				      0, 0, mode->hdisplay, mode->vdisplay };
		struct drm_mode_atomic at = { .flags = DRM_MODE_ATOMIC_ALLOW_MODESET, .count_objs = 3,
			.objs_ptr = U64(objs), .count_props_ptr = U64(counts), .props_ptr = U64(props),
			.prop_values_ptr = U64(vals) };
		if (xioctl(DRM_IOCTL_MODE_ATOMIC, &at) < 0) { ERR("ATOMIC fallo: errno %d (%s)\n", errno, strerror(errno)); return 1; }
		atomic = 1;
		LOG("ATOMIC ok\n");
	}

	LOG("mostrando %d s\n", secs);
	{
		struct timespec ts = { 0, 100 * 1000 * 1000 };
		for (long i = 0; !stop && i < (long)secs * 10; i++)
			nanosleep(&ts, NULL);
	}
	LOG("restaurando crtc original%s\n", stop ? " (interrumpido)" : "");

	struct drm_mode_crtc rc = saved;
	uint32_t saved_conn = conn_id;
	rc.set_connectors_ptr = U64(&saved_conn);
	rc.count_connectors = saved.mode_valid ? 1 : 0;
	if (!saved.mode_valid) rc.fb_id = 0;
	if (xioctl(DRM_IOCTL_MODE_SETCRTC, &rc) < 0) {
		ERR("SETCRTC (restaurar): errno %d (%s)\n", errno, strerror(errno));
		if (atomic) {
			uint32_t objs[3] = { conn_id, crtc_id, plane_id };
			uint32_t counts[3] = { 1, 2, 2 };
			uint32_t props[5] = { p_conn_crtc, p_crtc_mode, p_crtc_active, pl_fb, pl_crtc };
			uint64_t vals[5] = { 0, 0, 0, 0, 0 };
			struct drm_mode_atomic at = { .flags = DRM_MODE_ATOMIC_ALLOW_MODESET, .count_objs = 3,
				.objs_ptr = U64(objs), .count_props_ptr = U64(counts), .props_ptr = U64(props),
				.prop_values_ptr = U64(vals) };
			/* orden de props por objeto: conector[0], crtc[1..2], plano[3..4] */
			if (xioctl(DRM_IOCTL_MODE_ATOMIC, &at) < 0) ERR("ATOMIC (apagar): %s\n", strerror(errno));
			else LOG("crtc apagado por ATOMIC\n");
		}
	} else {
		LOG("crtc restaurado\n");
	}

	if (blob_id) { struct drm_mode_destroy_blob db = { .blob_id = blob_id }; xioctl(DRM_IOCTL_MODE_DESTROYPROPBLOB, &db); }
	xioctl(DRM_IOCTL_MODE_RMFB, &fb_id);
	munmap(map, cd.size);
	struct drm_mode_destroy_dumb dd = { .handle = cd.handle };
	xioctl(DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
	ioctl(fd, DRM_IOCTL_DROP_MASTER, 0);
	LOG("fb liberado, fin\n");
	return 0;
}
