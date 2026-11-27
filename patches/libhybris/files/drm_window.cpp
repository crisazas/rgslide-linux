#include "drm_window.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <drm/drm_fourcc.h>

#include <dlfcn.h>
extern "C" {
#include <hybris/common/binding.h>
}
#include "logging.h"

namespace {
struct RgAhbDesc { uint32_t width, height, layers, format; uint64_t usage; uint32_t stride, rfu0; uint64_t rfu1; };
struct RgNativeHandle { int version, numFds, numInts; int data[1]; };
typedef int (*ahb_alloc_t)(const RgAhbDesc *, AHardwareBuffer **);
typedef void (*ahb_release_t)(AHardwareBuffer *);
typedef void (*ahb_describe_t)(const AHardwareBuffer *, RgAhbDesc *);
typedef const RgNativeHandle *(*ahb_handle_t)(const AHardwareBuffer *);
ahb_alloc_t p_alloc; ahb_release_t p_release; ahb_describe_t p_describe; ahb_handle_t p_handle;
bool ahb_ready = false;

bool ahb_init()
{
	if (ahb_ready) return p_alloc && p_release && p_describe && p_handle;
	ahb_ready = true;
	void *h = android_dlopen("libnativewindow.so", RTLD_LAZY);
	if (!h) { fprintf(stderr, "hybris drm: no abro libnativewindow.so\n"); return false; }
	p_alloc = (ahb_alloc_t)android_dlsym(h, "AHardwareBuffer_allocate");
	p_release = (ahb_release_t)android_dlsym(h, "AHardwareBuffer_release");
	p_describe = (ahb_describe_t)android_dlsym(h, "AHardwareBuffer_describe");
	p_handle = (ahb_handle_t)android_dlsym(h, "AHardwareBuffer_getNativeHandle");
	if (!p_alloc || !p_release || !p_describe || !p_handle) { fprintf(stderr, "hybris drm: faltan simbolos AHardwareBuffer_*\n"); return false; }
	return true;
}
}

#define U64(p) ((uint64_t)(uintptr_t)(p))
#define AHB_USAGE_GPU_COLOR_OUTPUT (1ULL << 9)
#define AHB_USAGE_GPU_SAMPLED      (1ULL << 8)
#define AHB_USAGE_COMPOSER_OVERLAY (1ULL << 11)

static int dx(int fd, unsigned long r, void *a)
{
	int rc;
	do rc = ioctl(fd, r, a); while (rc < 0 && (errno == EINTR || errno == EAGAIN));
	return rc;
}

/* ------------------------------------------------------------------ buffer */

DrmNativeWindowBuffer::DrmNativeWindowBuffer(unsigned w, unsigned h, unsigned fmt, uint64_t usage)
{
	RgAhbDesc d, o;
	if (!ahb_init()) return;
	memset(&d, 0, sizeof d);
	d.width = w; d.height = h; d.layers = 1; d.format = fmt ? fmt : 1;
	/* El blob pide bits de uso privados (p. ej. bit 62) que gralloc rechaza (rc=5): solo se pasan los bits publicos de AHardwareBuffer. */
	d.usage = (usage & 0x0000FFFFFFFFULL & ~AHB_USAGE_GPU_SAMPLED) | AHB_USAGE_GPU_COLOR_OUTPUT | AHB_USAGE_COMPOSER_OVERLAY;
	int rc = p_alloc(&d, &m_ahb);
	if (rc != 0 || !m_ahb) { fprintf(stderr, "hybris drm: AHardwareBuffer_allocate rc=%d (%ux%u fmt=%u usage=0x%llx)\n", rc, w, h, d.format, (unsigned long long)d.usage); m_ahb = nullptr; return; }
	memset(&o, 0, sizeof o);
	p_describe(m_ahb, &o);
	const RgNativeHandle *h2 = p_handle(m_ahb);
	ANativeWindowBuffer::width = o.width;
	ANativeWindowBuffer::height = o.height;
	ANativeWindowBuffer::stride = o.stride;
	ANativeWindowBuffer::format = o.format;
	ANativeWindowBuffer::usage = (int)o.usage;
	ANativeWindowBuffer::handle = (buffer_handle_t)h2;
	m_dmabuf = (h2 && h2->numFds > 0) ? h2->data[0] : -1;
}

DrmNativeWindowBuffer::~DrmNativeWindowBuffer()
{
	if (getenv("HYBRIS_DRM_DEBUG")) fprintf(stderr, "drm: ~buffer ahb=%p release=%p\n", (void *)m_ahb, (void *)p_release);
	if (m_ahb && p_release) p_release(m_ahb);
	if (getenv("HYBRIS_DRM_DEBUG")) fprintf(stderr, "drm: ~buffer ok\n");
}

/* ------------------------------------------------------------------ window */

DrmNativeWindow::DrmNativeWindow()
{
	m_mode = calloc(1, sizeof(struct drm_mode_modeinfo));
	if (!openDrm()) {
		fprintf(stderr, "hybris drm: no pude abrir el panel\n");
		return;
	}
	m_w = m_reqW = ((struct drm_mode_modeinfo *)m_mode)->hdisplay;
	m_h = m_reqH = ((struct drm_mode_modeinfo *)m_mode)->vdisplay;
}

DrmNativeWindow::~DrmNativeWindow()
{
	if (getenv("HYBRIS_DRM_DEBUG")) fprintf(stderr, "drm: ~window inicio\n");
	freeBuffers();
	if (getenv("HYBRIS_DRM_DEBUG")) fprintf(stderr, "drm: ~window buffers libres\n");
	if (m_hi_on && m_drm >= 0) atomicMode(m_mode);
	if (m_drm >= 0) close(m_drm);
	free(m_mode);
	free(m_mode_hi);
	if (getenv("HYBRIS_DRM_DEBUG")) fprintf(stderr, "drm: ~window fin\n");
}

static int find_prop(int fd, uint32_t obj, uint32_t type, const char *name, uint32_t *id)
{
	struct drm_mode_obj_get_properties g;
	memset(&g, 0, sizeof g); g.obj_id = obj; g.obj_type = type;
	if (dx(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &g) < 0) return -1;
	uint32_t *ids = (uint32_t *)calloc(g.count_props + 1, 4); uint64_t *vals = (uint64_t *)calloc(g.count_props + 1, 8);
	g.props_ptr = U64(ids); g.prop_values_ptr = U64(vals);
	int r = -1;
	if (dx(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &g) == 0)
		for (uint32_t i = 0; i < g.count_props && r < 0; i++) {
			struct drm_mode_get_property gp; memset(&gp, 0, sizeof gp); gp.prop_id = ids[i];
			if (dx(fd, DRM_IOCTL_MODE_GETPROPERTY, &gp) == 0 && !strcmp(gp.name, name)) { *id = ids[i]; r = 0; }
		}
	free(ids); free(vals);
	return r;
}

/* El sprd-drm solo reprograma el DPU al cambiar de frecuencia si el commit lleva la propiedad CRTC "frame rate change"=1
 * (SETCRTC legacy con otro vrefresh no la pone y el panel sigue a 60 Hz). Ver docs/agent-refresh.md. */
int DrmNativeWindow::atomicMode(void *modep)
{
	uint32_t p_cid, p_mode, p_act, p_frc;
	struct drm_set_client_cap cc;
	struct drm_mode_create_blob cb;
	struct drm_mode_destroy_blob db;
	struct drm_mode_atomic at;
	uint32_t objs[2], counts[2] = { 1, 3 }, props[4];
	uint64_t vals[4];
	int r;
	memset(&cc, 0, sizeof cc); cc.capability = DRM_CLIENT_CAP_UNIVERSAL_PLANES; cc.value = 1; dx(m_drm, DRM_IOCTL_SET_CLIENT_CAP, &cc);
	cc.capability = DRM_CLIENT_CAP_ATOMIC;
	if (dx(m_drm, DRM_IOCTL_SET_CLIENT_CAP, &cc) < 0) return -1;
	if (find_prop(m_drm, m_conn, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID", &p_cid) || find_prop(m_drm, m_crtc, DRM_MODE_OBJECT_CRTC, "MODE_ID", &p_mode) ||
	    find_prop(m_drm, m_crtc, DRM_MODE_OBJECT_CRTC, "ACTIVE", &p_act) || find_prop(m_drm, m_crtc, DRM_MODE_OBJECT_CRTC, "frame rate change", &p_frc)) return -1;
	memset(&cb, 0, sizeof cb); cb.data = U64(modep); cb.length = sizeof(struct drm_mode_modeinfo);
	if (dx(m_drm, DRM_IOCTL_MODE_CREATEPROPBLOB, &cb) < 0) return -1;
	objs[0] = m_conn; objs[1] = m_crtc;
	props[0] = p_cid; props[1] = p_mode; props[2] = p_act; props[3] = p_frc;
	vals[0] = m_crtc; vals[1] = cb.blob_id; vals[2] = 1; vals[3] = 1;
	memset(&at, 0, sizeof at);
	at.flags = DRM_MODE_ATOMIC_ALLOW_MODESET; at.count_objs = 2; at.objs_ptr = U64(objs);
	at.count_props_ptr = U64(counts); at.props_ptr = U64(props); at.prop_values_ptr = U64(vals);
	r = dx(m_drm, DRM_IOCTL_MODE_ATOMIC, &at);
	memset(&db, 0, sizeof db); db.blob_id = cb.blob_id; dx(m_drm, DRM_IOCTL_MODE_DESTROYPROPBLOB, &db);
	return r;
}

void DrmNativeWindow::applyRefresh()
{
	struct drm_mode_modeinfo *hi = (struct drm_mode_modeinfo *)m_mode_hi;
	if (!hi) return;
	if (atomicMode(hi) == 0) { m_hi_on = true; fprintf(stderr, "hybris drm: modo %u Hz\n", hi->vrefresh); }
	else { m_hi_on = false; fprintf(stderr, "hybris drm: no pude pasar a %u Hz, sigo a 60\n", hi->vrefresh); }
}

bool DrmNativeWindow::openDrm()
{
	struct drm_mode_card_res res;
	uint32_t *crtcs, *conns, *encs;
	const char *dev = getenv("HYBRIS_DRM_DEVICE");
	const char *pref = getenv("HYBRIS_DRM_CONNECTOR");
	struct drm_mode_modeinfo *modes = nullptr, *mode = (struct drm_mode_modeinfo *)m_mode;
	uint32_t enc_id = 0, i;
	uint32_t *conn_encs = nullptr;
	uint32_t conn_enc_cnt = 0;
	int pick = -1;
	int want_dp = (!pref || !strcmp(pref, "auto") || !strcmp(pref, "dp"));
	int only_dsi = (pref && !strcmp(pref, "dsi"));

	m_drm = open(dev ? dev : "/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (m_drm < 0) return false;
	ioctl(m_drm, DRM_IOCTL_SET_MASTER, 0);
	memset(&res, 0, sizeof res);
	if (dx(m_drm, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) return false;
	crtcs = (uint32_t *)calloc(res.count_crtcs + 1, 4); conns = (uint32_t *)calloc(res.count_connectors + 1, 4); encs = (uint32_t *)calloc(res.count_encoders + 1, 4);
	res.crtc_id_ptr = U64(crtcs); res.connector_id_ptr = U64(conns); res.encoder_id_ptr = U64(encs); res.count_fbs = 0; res.fb_id_ptr = 0;
	if (dx(m_drm, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) return false;

	/* Two passes: pass 0 looks for external DisplayPort/HDMI (if auto/dp), pass 1 falls back to any connected (DSI). */
	for (int pass = (want_dp && !only_dsi) ? 0 : 1; pass < 2 && !m_conn; pass++) {
		for (i = 0; i < res.count_connectors && !m_conn; i++) {
			struct drm_mode_get_connector c;
			memset(&c, 0, sizeof c); c.connector_id = conns[i];
			if (dx(m_drm, DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0 || c.connection != 1 || !c.count_modes) continue;
			bool is_ext = (c.connector_type == 10 /* DP */ || c.connector_type == 11 /* HDMIA */ || c.connector_type == 12 /* HDMIB */);
			if (pass == 0 && !is_ext) continue;
			if (only_dsi && is_ext) continue;
			modes = (struct drm_mode_modeinfo *)calloc(c.count_modes, sizeof *modes);
			conn_enc_cnt = c.count_encoders;
			conn_encs = conn_enc_cnt ? (uint32_t *)calloc(conn_enc_cnt, 4) : nullptr;
			c.modes_ptr = U64(modes); c.count_props = 0; c.props_ptr = c.prop_values_ptr = 0;
			c.encoders_ptr = U64(conn_encs);
			if (dx(m_drm, DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0) { free(modes); free(conn_encs); modes = nullptr; conn_encs = nullptr; continue; }
			m_conn = conns[i]; enc_id = c.encoder_id;
			/* Prefer DRM_MODE_TYPE_PREFERRED (0x08) at 60Hz, then any 60Hz, then mode 0. */
			for (unsigned k = 0; k < c.count_modes; k++) if ((modes[k].type & 0x08) && modes[k].vrefresh == 60 && pick < 0) pick = (int)k;
			for (unsigned k = 0; k < c.count_modes; k++) if (modes[k].vrefresh == 60 && pick < 0) pick = (int)k;
			if (pick < 0) pick = 0;
			*mode = modes[pick];
			{
				const char *want_s = getenv("HYBRIS_DRM_REFRESH");
				unsigned want = want_s ? (unsigned)atoi(want_s) : 60;
				for (unsigned k = 0; want != 60 && k < c.count_modes; k++)
					if (modes[k].vrefresh == want && modes[k].hdisplay == mode->hdisplay && modes[k].vdisplay == mode->vdisplay) {
						m_mode_hi = malloc(sizeof *mode); memcpy(m_mode_hi, &modes[k], sizeof *mode); break;
					}
				if (want != 60 && !m_mode_hi) fprintf(stderr, "hybris drm: no hay modo a %u Hz, sigo a 60\n", want);
			}
		}
	}
	if (!m_conn) { free(crtcs); free(conns); free(encs); return false; }
	if (enc_id) {
		struct drm_mode_get_encoder e; memset(&e, 0, sizeof e); e.encoder_id = enc_id;
		if (dx(m_drm, DRM_IOCTL_MODE_GETENCODER, &e) == 0) m_crtc = e.crtc_id;
	}
	if (!m_crtc && conn_encs) {
		for (uint32_t j = 0; j < conn_enc_cnt && !m_crtc; j++) {
			struct drm_mode_get_encoder e; memset(&e, 0, sizeof e); e.encoder_id = conn_encs[j];
			if (dx(m_drm, DRM_IOCTL_MODE_GETENCODER, &e) == 0 && e.possible_crtcs) {
				for (uint32_t k = 0; k < res.count_crtcs; k++) {
					if (e.possible_crtcs & (1u << k)) { m_crtc = crtcs[k]; break; }
				}
			}
		}
	}
	if (!m_crtc) m_crtc = crtcs[0];
	m_hz = mode->vrefresh;
	free(crtcs); free(conns); free(encs); free(modes); free(conn_encs);
	fprintf(stderr, "hybris drm: panel %ux%u@%u crtc %u conn %u\n", mode->hdisplay, mode->vdisplay, mode->vrefresh, m_crtc, m_conn);
	return true;
}

void DrmNativeWindow::freeBuffers()
{
	for (auto *b : m_bufs) {
		if (b->m_fb) { uint32_t fb = b->m_fb; dx(m_drm, DRM_IOCTL_MODE_RMFB, &fb); b->m_fb = 0; }
		b->common.decRef(&b->common);
	}
	m_bufs.clear();
	m_front = m_pending = nullptr;
}

bool DrmNativeWindow::importFb(DrmNativeWindowBuffer *b)
{
	struct drm_prime_handle ph;
	struct drm_mode_fb_cmd2 f2;
	if (b->m_fb) return true;
	if (b->m_dmabuf < 0) return false;
	memset(&ph, 0, sizeof ph); ph.fd = b->m_dmabuf;
	if (dx(m_drm, DRM_IOCTL_PRIME_FD_TO_HANDLE, &ph) < 0) { fprintf(stderr, "hybris drm: PRIME_FD_TO_HANDLE: %s\n", strerror(errno)); return false; }
	memset(&f2, 0, sizeof f2);
	f2.width = b->width; f2.height = b->height; f2.pixel_format = DRM_FORMAT_ABGR8888;
	f2.handles[0] = ph.handle; f2.pitches[0] = b->stride * 4;
	if (dx(m_drm, DRM_IOCTL_MODE_ADDFB2, &f2) < 0) { fprintf(stderr, "hybris drm: ADDFB2: %s\n", strerror(errno)); return false; }
	b->m_fb = f2.fb_id;
	return true;
}

int DrmNativeWindow::waitFlip()
{
	struct pollfd pf;
	char ev[256];
	pf.fd = m_drm; pf.events = POLLIN;
	for (;;) {
		int r = poll(&pf, 1, 300);
		if (r <= 0) return -1;
		r = (int)read(m_drm, ev, sizeof ev);
		for (int o = 0; r > 0 && o + 8 <= r;) {
			struct drm_event *e = (struct drm_event *)(ev + o);
			if (e->type == DRM_EVENT_FLIP_COMPLETE) return 0;
			if (e->length < 8) break;
			o += e->length;
		}
	}
}

int DrmNativeWindow::setSwapInterval(int interval) { m_interval = interval; return 0; }

int DrmNativeWindow::dequeueBuffer(BaseNativeWindowBuffer **buffer, int *fenceFd)
{
	if (m_bufs.empty()) {
		for (unsigned i = 0; i < m_count; i++) {
			DrmNativeWindowBuffer *b = new DrmNativeWindowBuffer(m_reqW, m_reqH, m_format, m_usage);
			if (!b->ok()) { fprintf(stderr, "hybris drm: AHardwareBuffer_allocate fallo (%ux%u)\n", m_reqW, m_reqH); delete b; freeBuffers(); return -1; }
			b->common.incRef(&b->common);
			m_bufs.push_back(b);
		}
		m_w = m_reqW; m_h = m_reqH;
	}
	for (auto *b : m_bufs) {
		if (!b->m_busy) {
			b->m_busy = true;
			*buffer = b;
			*fenceFd = -1;
			return NO_ERROR;
		}
	}
	return -1;
}

int DrmNativeWindow::queueBuffer(BaseNativeWindowBuffer *buffer, int fenceFd)
{
	DrmNativeWindowBuffer *b = static_cast<DrmNativeWindowBuffer *>(buffer);
	struct drm_mode_modeinfo *mode = (struct drm_mode_modeinfo *)m_mode;
	if (fenceFd >= 0) { struct pollfd p; p.fd = fenceFd; p.events = POLLIN; poll(&p, 1, 1000); close(fenceFd); }
	if (!importFb(b)) { b->m_busy = false; return -1; }
	if (!m_modeset) {
		struct drm_mode_crtc sc;
		memset(&sc, 0, sizeof sc);
		sc.crtc_id = m_crtc; sc.fb_id = b->m_fb; sc.set_connectors_ptr = U64(&m_conn); sc.count_connectors = 1; sc.mode_valid = 1; sc.mode = *mode;
		if (dx(m_drm, DRM_IOCTL_MODE_SETCRTC, &sc) < 0) { fprintf(stderr, "hybris drm: SETCRTC: %s\n", strerror(errno)); b->m_busy = false; return -1; }
		m_modeset = true; m_front = b; m_hi_on = false;
		applyRefresh();
		return NO_ERROR;
	}
	if (m_pending) {
		waitFlip();
		if (m_front) m_front->m_busy = false;
		m_front = m_pending; m_pending = nullptr;
	}
	{
		struct drm_mode_crtc_page_flip pf;
		memset(&pf, 0, sizeof pf);
		pf.crtc_id = m_crtc; pf.fb_id = b->m_fb; pf.flags = DRM_MODE_PAGE_FLIP_EVENT;
		if (dx(m_drm, DRM_IOCTL_MODE_PAGE_FLIP, &pf) < 0) { fprintf(stderr, "hybris drm: PAGE_FLIP: %s\n", strerror(errno)); b->m_busy = false; return -1; }
		m_pending = b;
	}
	return NO_ERROR;
}

int DrmNativeWindow::cancelBuffer(BaseNativeWindowBuffer *buffer, int fenceFd)
{
	if (fenceFd >= 0) close(fenceFd);
	static_cast<DrmNativeWindowBuffer *>(buffer)->m_busy = false;
	return NO_ERROR;
}

int DrmNativeWindow::lockBuffer(BaseNativeWindowBuffer *) { return NO_ERROR; }
unsigned int DrmNativeWindow::type() const { return NATIVE_WINDOW_SURFACE; }
unsigned int DrmNativeWindow::width() const { return m_reqW; }
unsigned int DrmNativeWindow::height() const { return m_reqH; }
unsigned int DrmNativeWindow::format() const { return m_format; }
unsigned int DrmNativeWindow::defaultWidth() const { return m_w; }
unsigned int DrmNativeWindow::defaultHeight() const { return m_h; }
unsigned int DrmNativeWindow::queueLength() const { return 1; }
unsigned int DrmNativeWindow::transformHint() const { return 0; }
unsigned int DrmNativeWindow::getUsage() const { return (unsigned)m_usage; }
int DrmNativeWindow::setUsage(uint64_t usage) { m_usage = usage; return NO_ERROR; }
int DrmNativeWindow::setBuffersFormat(int format) { m_format = format; return NO_ERROR; }
int DrmNativeWindow::setBufferCount(int cnt) { if (cnt >= 2 && cnt <= 6) m_count = cnt; return NO_ERROR; }
int DrmNativeWindow::setBuffersDimensions(int width, int height)
{
	if (width <= 0 || height <= 0) return NO_ERROR;
	if ((unsigned)width != m_reqW || (unsigned)height != m_reqH) {
		m_reqW = width; m_reqH = height;
		if (!m_bufs.empty()) { m_pending = nullptr; freeBuffers(); m_modeset = false; }
	}
	return NO_ERROR;
}
