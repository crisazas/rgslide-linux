/*
 * rgslide-vpu: Hardware video decoder bridge for Anbernic RG Slide (Unisoc T820 VPU).
 *
 * Uses the stock 5.4.233 kernel VPU driver (/dev/sprd_vsp, /dev/ion) and loads the
 * device's 64-bit Android hardware OMX decoder libraries via libhybris:
 *   - libomx_avcdec_hw_sprd.so  (H.264 / AVC, up to 4096x4096)
 *   - libomx_hevcdec_hw_sprd.so (H.265 / HEVC, up to 4096x4096)
 *   - libomx_vp9dec_hw_sprd.so  (VP9, up to 4096x4096)
 *   - libomx_vpxdec_hw_sprd.so  (VP8, up to 1920x1088)
 *   - libjpeg_hw_sprd.so        (JPEG hardware codec via /dev/sprd_jpg)
 *
 * Usage:
 *   rgslide-vpu info
 *   rgslide-vpu decode --codec h264|hevc -i <input.264|265> [-o <out.nv12|->] [--max-frames N] [--verbose]
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* libhybris bionic loader */
extern void *android_dlopen(const char *filename, int flag);
extern void *android_dlsym(void *handle, const char *symbol);
extern const char *android_dlerror(void);

/* Linux 4.12+ / Android ION allocation ioctl */
struct ion_allocation_data {
    uint64_t len;
    uint32_t heap_id_mask;
    uint32_t flags;
    uint32_t fd;
    uint32_t unused;
};
#define ION_IOC_ALLOC _IOWR('I', 0, struct ion_allocation_data)

/* Unisoc VPU /dev/sprd_vsp probe ioctls */
#define VPU_IOCTL_HW_INFO          0x6d0a
#define VPU_IOCTL_GET_IOMMU_STATUS 0x6d0d

/* -------------------------------------------------------------------------
 * Unisoc 64-bit libomx_{avc,hevc}dec_hw_sprd.so C ABI
 * Verified directly against RG Slide / T820 vendor/lib64 binaries.
 * ------------------------------------------------------------------------- */

typedef int (*vsp_bind_cb_t)(void *userData, void *pHeader);
typedef int (*vsp_unbind_cb_t)(void *userData, void *pHeader);
typedef int (*vsp_ext_mem_cb_t)(void *userData, uint32_t size);
typedef int (*vsp_mbinfo_mem_cb_t)(void *userData, uint32_t size, unsigned long *iova_out);

typedef struct {
    void *videoDecoderData;        /* +0 */
    void *userData;                /* +8 */
    vsp_bind_cb_t bindCb;          /* +16 */
    vsp_unbind_cb_t unbindCb;      /* +24 */
    vsp_ext_mem_cb_t extMemCb;     /* +32: allocates internal HW workspace & calls DecMemInit */
    vsp_mbinfo_mem_cb_t mbinfoCb;  /* +40: allocates per-frame MV/MBinfo ION buffer & returns IOVA */
    void *reserved[6];
} VpuDecHandle;

typedef struct {
    uint8_t *common_buffer_ptr;     /* +0 */
    uint64_t common_buffer_ptr_phy; /* +8 */
    uint32_t size;                  /* +16 */
    uint32_t pad0;                  /* +20 */
    uint8_t *int_buffer_ptr_legacy; /* +24 */
    uint64_t pad1;                  /* +32 */
    uint8_t *int_buffer_ptr;        /* +40 (used by H264DecMemInit / H265DecMemInit) */
    uint64_t int_buffer_ptr_phy;    /* +48 */
    uint32_t int_size;              /* +56 */
    uint32_t pad2;
} MMCodecBuffer;

typedef struct {
    int32_t video_std;      /* +0 */
    int32_t frame_width;    /* +4 */
    int32_t frame_height;   /* +8 */
    int32_t i_extra;        /* +12 */
    void *p_extra;          /* +16 */
    uint64_t p_extra_phy;   /* +24 */
    int32_t yuv_format;     /* +32: 0=YUV420SP(NV12) */
    int32_t reserved[15];
} MMDecVideoFormat;

typedef struct {
    int32_t thumb_flag;     /* +0 */
    int32_t afbc_flag;      /* +4: 0=linear NV12 */
    int32_t reserved[14];
} MMDecInitExt;

typedef struct {
    uint8_t *pStream;       /* +0 */
    uint64_t pStream_phy;   /* +8 */
    uint32_t dataLen;       /* +16 */
    int32_t beLastFrm;      /* +20 */
    int32_t expected_IVOP;  /* +24 */
    int32_t pad0;           /* +28 */
    int64_t pts;            /* +32 */
    uint8_t pad1[128];      /* +40..167 */
    uint8_t need_deint;     /* +168 */
    uint8_t pad2[87];       /* total 256 bytes */
} MMDecInput;

typedef struct {
    uint8_t *pOutFrameY;    /* +0 */
    uint8_t *pOutFrameU;    /* +8 */
    uint8_t *pOutFrameV;    /* +16 */
    uint32_t frame_width;   /* +24 */
    uint32_t frame_height;  /* +28 */
    int32_t is_transposed;  /* +32 */
    int32_t pad0;           /* +36 */
    int64_t pts;            /* +40 */
    int32_t frameEffective; /* +48 */
    int32_t pad1;           /* +52 */
    void *pBufferHeader;    /* +56 */
    int32_t flag;           /* +64 */
    uint32_t picId;         /* +68 */
    uint8_t flags[4];       /* +72 */
    int32_t released_ids[16]; /* +76..139 */
    int32_t num_released;   /* +140 */
    uint8_t pad2[112];      /* total 256 bytes */
} MMDecOutput;

typedef struct {
    int32_t profile;        /* +0 */
    uint32_t picWidth;      /* +4 */
    uint32_t picHeight;     /* +8 */
    uint32_t videoRange;    /* +12 */
    uint32_t matrixCoeffs;  /* +16 */
    uint32_t reserved0[2];  /* +20..27 */
    int32_t croppingFlag;   /* +28 */
    uint32_t cropLeft;      /* +32 */
    uint32_t cropWidth;     /* +36 */
    uint32_t cropTop;       /* +40 */
    uint32_t cropHeight;    /* +44 */
    uint32_t numRefFrames;  /* +48 */
    uint32_t has_b_frames;  /* +52 */
    uint32_t interlaced;    /* +56 */
    uint8_t extra[196];
} DecStreamInfo;

/* Function pointer table for unified H264 / H265 HW decoder */
typedef int (*fn_dec_init_t)(VpuDecHandle *, MMCodecBuffer *, MMDecVideoFormat *, MMDecInitExt *);
typedef int (*fn_dec_mem_init_t)(VpuDecHandle *, MMCodecBuffer *);
typedef int (*fn_dec_get_info_t)(VpuDecHandle *, DecStreamInfo *);
typedef void (*fn_dec_set_rec_h264_t)(VpuDecHandle *, uint8_t *, uint64_t, void *, uint32_t, uint32_t);
typedef void (*fn_dec_set_rec_h265_t)(VpuDecHandle *, uint8_t *, uint64_t, uint64_t, void *, uint32_t, uint32_t);
typedef int (*fn_dec_decode_t)(VpuDecHandle *, MMDecInput *, MMDecOutput *);
typedef int (*fn_dec_get_last_t)(VpuDecHandle *, void **, uint32_t *, int64_t *);
typedef int (*fn_dec_release_t)(VpuDecHandle *);
typedef int (*fn_dec_get_iova_t)(VpuDecHandle *, int fd, unsigned long *iova, size_t *size, int need_acquire);
typedef int (*fn_dec_free_iova_t)(VpuDecHandle *, unsigned long iova, size_t size, int need_acquire);

typedef struct {
    const char *name;
    const char *lib_name;
    bool is_hevc;
    void *dl;
    fn_dec_init_t init;
    fn_dec_mem_init_t mem_init;
    fn_dec_get_info_t get_info;
    fn_dec_set_rec_h264_t set_rec_h264;
    fn_dec_set_rec_h265_t set_rec_h265;
    fn_dec_decode_t decode;
    fn_dec_get_last_t get_last;
    fn_dec_release_t release;
    fn_dec_get_iova_t get_iova;
    fn_dec_free_iova_t free_iova;
} CodecOps;

static void codec_set_rec(CodecOps *ops, VpuDecHandle *h, uint8_t *va, uint64_t iova,
                          void *hdr, uint32_t pic_id) {
    if (ops->is_hevc) {
        ops->set_rec_h265(h, va, iova, 0, hdr, pic_id, 0);
    } else {
        ops->set_rec_h264(h, va, iova, hdr, pic_id, 0);
    }
}

/* ION DMA-BUF wrapper */
typedef struct {
    int dma_fd;
    uint8_t *va;
    size_t size;
    unsigned long iova;
    size_t iova_size;
    bool iova_mapped;
} IonBuf;

static int g_ion_fd = -1;

static int ion_alloc_buf(IonBuf *b, size_t size) {
    memset(b, 0, sizeof(*b));
    b->dma_fd = -1;
    if (g_ion_fd < 0) {
        g_ion_fd = open("/dev/ion", O_RDWR | O_CLOEXEC);
        if (g_ion_fd < 0) {
            fprintf(stderr, "rgslide-vpu: open(/dev/ion) failed: %s\n", strerror(errno));
            return -1;
        }
    }
    /* Align to 4096 */
    size = (size + 4095u) & ~4095u;
    struct ion_allocation_data alloc;
    memset(&alloc, 0, sizeof(alloc));
    alloc.len = size;
    alloc.heap_id_mask = 1; /* ION system heap */
    alloc.flags = 0;
    if (ioctl(g_ion_fd, ION_IOC_ALLOC, &alloc) < 0) {
        fprintf(stderr, "rgslide-vpu: ION_IOC_ALLOC(%zu) failed: %s\n", size, strerror(errno));
        return -1;
    }
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, (int)alloc.fd, 0);
    if (p == MAP_FAILED) {
        fprintf(stderr, "rgslide-vpu: mmap ION fd=%u failed: %s\n", alloc.fd, strerror(errno));
        close((int)alloc.fd);
        return -1;
    }
    b->dma_fd = (int)alloc.fd;
    b->va = (uint8_t *)p;
    b->size = size;
    return 0;
}

static int ion_map_vpu(IonBuf *b, CodecOps *ops, VpuDecHandle *h, int need_acquire) {
    unsigned long iova = 0;
    size_t msize = 0;
    int rc = ops->get_iova(h, b->dma_fd, &iova, &msize, need_acquire);
    if (rc != 0) {
        fprintf(stderr, "rgslide-vpu: get_iova(fd=%d, acq=%d) failed rc=%d\n", b->dma_fd, need_acquire, rc);
        return rc;
    }
    b->iova = iova;
    b->iova_size = msize;
    b->iova_mapped = true;
    return 0;
}

static void ion_free_buf(IonBuf *b, CodecOps *ops, VpuDecHandle *h) {
    if (b->iova_mapped && ops && ops->free_iova && h && h->videoDecoderData) {
        ops->free_iova(h, b->iova, b->iova_size, 1);
        b->iova_mapped = false;
    }
    if (b->va && b->size) {
        munmap(b->va, b->size);
        b->va = NULL;
    }
    if (b->dma_fd >= 0) {
        close(b->dma_fd);
        b->dma_fd = -1;
    }
}

/* Frame buffer pool slot */
#define MAX_FRAME_SLOTS 24
#define MAX_MBINFO_BUFS 64
typedef struct {
    uint32_t id;
    IonBuf buf;
    int ref_count;
    bool in_use;
} FrameSlot;

typedef struct {
    FrameSlot slots[MAX_FRAME_SLOTS];
    int num_slots;
    IonBuf ext_buf;
    bool ext_buf_ready;
    IonBuf mbinfo_bufs[MAX_MBINFO_BUFS];
    int num_mbinfo;
    CodecOps *ops;
    VpuDecHandle *handle;
    bool verbose;
} PoolCtx;

static int cb_bind_frame(void *userData, void *pHeader) {
    PoolCtx *pool = (PoolCtx *)userData;
    FrameSlot *slot = (FrameSlot *)pHeader;
    if (slot && slot != (FrameSlot *)0x1) {
        slot->ref_count++;
        slot->in_use = true;
        if (pool && pool->verbose)
            fprintf(stderr, "  [vpu] bind slot %u (ref=%d)\n", slot->id, slot->ref_count);
    }
    return 0;
}

static int cb_unbind_frame(void *userData, void *pHeader) {
    PoolCtx *pool = (PoolCtx *)userData;
    FrameSlot *slot = (FrameSlot *)pHeader;
    if (slot && slot != (FrameSlot *)0x1) {
        if (slot->ref_count > 0)
            slot->ref_count--;
        if (slot->ref_count == 0)
            slot->in_use = false;
        if (pool && pool->verbose)
            fprintf(stderr, "  [vpu] unbind slot %u (ref=%d)\n", slot->id, slot->ref_count);
    }
    return 0;
}

static int cb_ext_mem(void *userData, uint32_t size) {
    PoolCtx *pool = (PoolCtx *)userData;
    size_t want = size > (4u * 1024u * 1024u) ? (size_t)size : (4u * 1024u * 1024u);
    want = (want + 65535u) & ~65535u;
    if (!pool->ext_buf_ready || pool->ext_buf.size < want) {
        if (pool->ext_buf_ready) {
            ion_free_buf(&pool->ext_buf, pool->ops, pool->handle);
            pool->ext_buf_ready = false;
        }
        if (ion_alloc_buf(&pool->ext_buf, want) != 0 ||
            ion_map_vpu(&pool->ext_buf, pool->ops, pool->handle, 0) != 0) {
            return -1;
        }
        pool->ext_buf_ready = true;
    }
    MMCodecBuffer cbuf;
    memset(&cbuf, 0, sizeof(cbuf));
    cbuf.int_buffer_ptr = pool->ext_buf.va;
    cbuf.int_buffer_ptr_phy = pool->ext_buf.iova;
    cbuf.int_size = (uint32_t)pool->ext_buf.size;
    int rc = pool->ops->mem_init(pool->handle, &cbuf);
    if (pool->verbose) {
        fprintf(stderr, "  [vpu] extMemCb(req=%u alloc=%zu iova=0x%lx) -> mem_init rc=%d\n",
                size, pool->ext_buf.size, pool->ext_buf.iova, rc);
    }
    return rc;
}

static int cb_mbinfo_mem(void *userData, uint32_t size, unsigned long *iova_out) {
    PoolCtx *pool = (PoolCtx *)userData;
    if (!iova_out || size == 0 || pool->num_mbinfo >= MAX_MBINFO_BUFS)
        return -1;
    IonBuf *b = &pool->mbinfo_bufs[pool->num_mbinfo];
    if (ion_alloc_buf(b, (size_t)size + 4096u) != 0 ||
        ion_map_vpu(b, pool->ops, pool->handle, 0) != 0) {
        return -1;
    }
    pool->num_mbinfo++;
    *iova_out = b->iova;
    if (pool->verbose) {
        fprintf(stderr, "  [vpu] mbinfoCb(req=%u) -> slot#%d iova=0x%lx\n",
                size, pool->num_mbinfo - 1, b->iova);
    }
    return 0;
}

static FrameSlot *pool_acquire_free(PoolCtx *pool) {
    for (int i = 0; i < pool->num_slots; i++) {
        if (!pool->slots[i].in_use && pool->slots[i].ref_count == 0) {
            return &pool->slots[i];
        }
    }
    /* Fallback: pick slot with lowest ref_count */
    for (int i = 0; i < pool->num_slots; i++) {
        if (pool->slots[i].ref_count == 0)
            return &pool->slots[i];
    }
    return &pool->slots[0];
}

static int load_codec(const char *codec, CodecOps *ops) {
    memset(ops, 0, sizeof(*ops));
    if (strcmp(codec, "h264") == 0 || strcmp(codec, "avc") == 0) {
        ops->name = "h264";
        ops->lib_name = "libomx_avcdec_hw_sprd.so";
        ops->dl = android_dlopen(ops->lib_name, RTLD_NOW);
        if (!ops->dl) {
            fprintf(stderr, "rgslide-vpu: android_dlopen(%s) failed: %s\n", ops->lib_name, android_dlerror());
            return -1;
        }
        ops->is_hevc = false;
        ops->init = (fn_dec_init_t)android_dlsym(ops->dl, "H264DecInit");
        ops->mem_init = (fn_dec_mem_init_t)android_dlsym(ops->dl, "H264DecMemInit");
        ops->get_info = (fn_dec_get_info_t)android_dlsym(ops->dl, "H264DecGetInfo");
        ops->set_rec_h264 = (fn_dec_set_rec_h264_t)android_dlsym(ops->dl, "H264Dec_SetCurRecPic");
        ops->decode = (fn_dec_decode_t)android_dlsym(ops->dl, "H264DecDecode");
        ops->get_last = (fn_dec_get_last_t)android_dlsym(ops->dl, "H264Dec_GetLastDspFrm");
        ops->release = (fn_dec_release_t)android_dlsym(ops->dl, "H264DecRelease");
        ops->get_iova = (fn_dec_get_iova_t)android_dlsym(ops->dl, "H264Dec_get_iova");
        ops->free_iova = (fn_dec_free_iova_t)android_dlsym(ops->dl, "H264Dec_free_iova");
    } else if (strcmp(codec, "hevc") == 0 || strcmp(codec, "h265") == 0) {
        ops->name = "hevc";
        ops->lib_name = "libomx_hevcdec_hw_sprd.so";
        ops->is_hevc = true;
        ops->dl = android_dlopen(ops->lib_name, RTLD_NOW);
        if (!ops->dl) {
            fprintf(stderr, "rgslide-vpu: android_dlopen(%s) failed: %s\n", ops->lib_name, android_dlerror());
            return -1;
        }
        ops->init = (fn_dec_init_t)android_dlsym(ops->dl, "H265DecInit");
        ops->mem_init = (fn_dec_mem_init_t)android_dlsym(ops->dl, "H265DecMemInit");
        ops->get_info = (fn_dec_get_info_t)android_dlsym(ops->dl, "H265DecGetInfo");
        ops->set_rec_h265 = (fn_dec_set_rec_h265_t)android_dlsym(ops->dl, "H265Dec_SetCurRecPic");
        ops->decode = (fn_dec_decode_t)android_dlsym(ops->dl, "H265DecDecode");
        ops->get_last = (fn_dec_get_last_t)android_dlsym(ops->dl, "H265Dec_GetLastDspFrm");
        ops->release = (fn_dec_release_t)android_dlsym(ops->dl, "H265DecRelease");
        ops->get_iova = (fn_dec_get_iova_t)android_dlsym(ops->dl, "H265Dec_get_iova");
        ops->free_iova = (fn_dec_free_iova_t)android_dlsym(ops->dl, "H265Dec_free_iova");
    } else {
        fprintf(stderr, "rgslide-vpu: unsupported codec '%s' (use h264 or hevc)\n", codec);
        return -1;
    }

    if (!ops->init || !ops->mem_init || !ops->get_info ||
        (!ops->is_hevc && !ops->set_rec_h264) || (ops->is_hevc && !ops->set_rec_h265) ||
        !ops->decode || !ops->get_last || !ops->release || !ops->get_iova || !ops->free_iova) {
        fprintf(stderr, "rgslide-vpu: missing symbol(s) in %s\n", ops->lib_name);
        return -1;
    }
    return 0;
}

/* Annex-B NALU scanner */
static size_t find_start_code(const uint8_t *buf, size_t len, size_t from, size_t *sc_len) {
    for (size_t i = from; i + 3 <= len; i++) {
        if (buf[i] == 0 && buf[i + 1] == 0) {
            if (buf[i + 2] == 1) {
                if (sc_len) *sc_len = 3;
                return i;
            }
            if (i + 4 <= len && buf[i + 2] == 0 && buf[i + 3] == 1) {
                if (sc_len) *sc_len = 4;
                return i;
            }
        }
    }
    return len;
}

typedef struct {
    size_t offset;
    size_t size;
    int nal_type;
    bool is_vcl;
    bool is_first_slice;
} NaluSpan;

static int parse_nalus(const uint8_t *data, size_t len, bool is_hevc, NaluSpan **out_nalus) {
    size_t cap = 256, cnt = 0;
    NaluSpan *arr = malloc(cap * sizeof(NaluSpan));
    if (!arr) return -1;

    size_t sc_len = 0;
    size_t pos = find_start_code(data, len, 0, &sc_len);
    while (pos < len) {
        size_t next_sc = 0;
        size_t next = find_start_code(data, len, pos + sc_len, &next_sc);
        size_t nalu_start = pos + sc_len;
        if (nalu_start < next) {
            if (cnt == cap) {
                cap *= 2;
                arr = realloc(arr, cap * sizeof(NaluSpan));
                if (!arr) return -1;
            }
            NaluSpan *n = &arr[cnt++];
            n->offset = pos;
            n->size = next - pos;
            if (!is_hevc) {
                n->nal_type = data[nalu_start] & 0x1f;
                n->is_vcl = (n->nal_type >= 1 && n->nal_type <= 5);
                /* H.264 first_mb_in_slice == 0 => MSB of first slice byte is 1 */
                n->is_first_slice = n->is_vcl && (nalu_start + 1 < next) && ((data[nalu_start + 1] & 0x80) != 0);
            } else {
                n->nal_type = (data[nalu_start] >> 1) & 0x3f;
                n->is_vcl = (n->nal_type <= 31);
                /* HEVC first_slice_segment_in_pic_flag is MSB of byte after 2-byte NAL header */
                n->is_first_slice = n->is_vcl && (nalu_start + 2 < next) && ((data[nalu_start + 2] & 0x80) != 0);
            }
        }
        pos = next;
        sc_len = next_sc;
    }
    *out_nalus = arr;
    return (int)cnt;
}

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void write_nv12_frame(FILE *out, const uint8_t *y_ptr, uint32_t stride_w, uint32_t stride_h,
                             uint32_t disp_w, uint32_t disp_h) {
    if (!out || !y_ptr || !stride_w || !stride_h) return;
    if (!disp_w || disp_w > stride_w) disp_w = stride_w;
    if (!disp_h || disp_h > stride_h) disp_h = stride_h;
    const uint8_t *uv_ptr = y_ptr + (size_t)stride_w * (size_t)stride_h;
    if (disp_w == stride_w) {
        fwrite(y_ptr, 1, (size_t)disp_w * (size_t)disp_h, out);
        fwrite(uv_ptr, 1, (size_t)disp_w * (size_t)(disp_h / 2), out);
    } else {
        for (uint32_t y = 0; y < disp_h; y++)
            fwrite(y_ptr + (size_t)y * stride_w, 1, disp_w, out);
        for (uint32_t y = 0; y < disp_h / 2; y++)
            fwrite(uv_ptr + (size_t)y * stride_w, 1, disp_w, out);
    }
}

static int cmd_info(void) {
    printf("rgslide-vpu: Unisoc T820 hardware video codec probe\n");
    int fd = open("/dev/sprd_vsp", O_RDWR);
    if (fd < 0) {
        printf("  /dev/sprd_vsp: MISSING (%s)\n", strerror(errno));
    } else {
        uint32_t ver = 0;
        int r1 = ioctl(fd, VPU_IOCTL_HW_INFO, &ver);
        int r2 = ioctl(fd, VPU_IOCTL_GET_IOMMU_STATUS, 0);
        printf("  /dev/sprd_vsp: OK (hw_version=0x%x/%u, iommu_status=%d, rc=%d)\n", ver, ver, r2, r1);
        close(fd);
    }
    const char *devs[] = { "/dev/vpu_enc0", "/dev/vpu_enc1", "/dev/sprd_jpg", "/dev/ion" };
    for (int i = 0; i < 4; i++) {
        int dfd = open(devs[i], O_RDWR);
        printf("  %-14s %s\n", devs[i], dfd >= 0 ? "OK" : strerror(errno));
        if (dfd >= 0) close(dfd);
    }
    const char *libs[][2] = {
        { "H.264 HW dec", "libomx_avcdec_hw_sprd.so" },
        { "H.265 HW dec", "libomx_hevcdec_hw_sprd.so" },
        { "VP9   HW dec", "libomx_vp9dec_hw_sprd.so" },
        { "VP8   HW dec", "libomx_vpxdec_hw_sprd.so" },
        { "JPEG  HW    ", "libjpeg_hw_sprd.so" },
    };
    for (int i = 0; i < 5; i++) {
        void *dl = android_dlopen(libs[i][1], RTLD_NOW);
        printf("  %s (%s): %s\n", libs[i][0], libs[i][1], dl ? "READY" : android_dlerror());
    }
    return 0;
}

static int cmd_decode(const char *codec, const char *in_path, const char *out_path, int max_frames, bool verbose) {
    bool is_hevc = (strcmp(codec, "hevc") == 0 || strcmp(codec, "h265") == 0);
    CodecOps ops;
    if (load_codec(codec, &ops) != 0) return 1;

    /* Read elementary stream */
    FILE *fin = fopen(in_path, "rb");
    if (!fin) {
        fprintf(stderr, "rgslide-vpu: cannot open %s: %s\n", in_path, strerror(errno));
        return 1;
    }
    fseek(fin, 0, SEEK_END);
    long fsz = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    if (fsz <= 0) {
        fprintf(stderr, "rgslide-vpu: empty input %s\n", in_path);
        fclose(fin);
        return 1;
    }
    uint8_t *file_buf = malloc((size_t)fsz);
    if (!file_buf || fread(file_buf, 1, (size_t)fsz, fin) != (size_t)fsz) {
        fprintf(stderr, "rgslide-vpu: read failed\n");
        fclose(fin);
        free(file_buf);
        return 1;
    }
    fclose(fin);

    NaluSpan *nalus = NULL;
    int num_nalus = parse_nalus(file_buf, (size_t)fsz, is_hevc, &nalus);
    if (num_nalus <= 0) {
        fprintf(stderr, "rgslide-vpu: no Annex-B NALUs found in %s\n", in_path);
        free(file_buf);
        return 1;
    }

    FILE *fout = NULL;
    if (out_path) {
        if (strcmp(out_path, "-") == 0) {
            fout = stdout;
        } else {
            fout = fopen(out_path, "wb");
            if (!fout) {
                fprintf(stderr, "rgslide-vpu: cannot open output %s: %s\n", out_path, strerror(errno));
                free(nalus);
                free(file_buf);
                return 1;
            }
        }
    }

    PoolCtx pool;
    memset(&pool, 0, sizeof(pool));
    pool.verbose = verbose;
    pool.ops = &ops;

    VpuDecHandle handle;
    memset(&handle, 0, sizeof(handle));
    pool.handle = &handle;
    handle.userData = &pool;
    handle.bindCb = cb_bind_frame;
    handle.unbindCb = cb_unbind_frame;
    handle.extMemCb = cb_ext_mem;
    handle.mbinfoCb = cb_mbinfo_mem;

    /* Allocate internal decoder CPU buffer (2 MiB) */
    const size_t common_sz = 2 * 1024 * 1024;
    uint8_t *common_mem = aligned_alloc(64, common_sz);
    memset(common_mem, 0, common_sz);

    MMCodecBuffer cbuf;
    memset(&cbuf, 0, sizeof(cbuf));
    cbuf.common_buffer_ptr = common_mem;
    cbuf.common_buffer_ptr_phy = 0;
    cbuf.size = (uint32_t)common_sz;

    MMDecVideoFormat vfmt;
    memset(&vfmt, 0, sizeof(vfmt));
    vfmt.video_std = is_hevc ? 5 : 3;
    vfmt.yuv_format = 0; /* NV12 / YUV420SP */

    MMDecInitExt init_ext;
    memset(&init_ext, 0, sizeof(init_ext));
    init_ext.thumb_flag = 0;
    init_ext.afbc_flag = 0; /* Linear NV12 */

    int rc = ops.init(&handle, &cbuf, &vfmt, &init_ext);
    if (rc != 0) {
        fprintf(stderr, "rgslide-vpu: %s Init failed rc=%d\n", ops.name, rc);
        free(common_mem);
        free(nalus);
        free(file_buf);
        return 1;
    }

    /* Allocate ION hardware internal workspace (24 MiB, covers up to 4K DPB/MV/CABAC tables) */
    IonBuf hw_int_buf;
    if (ion_alloc_buf(&hw_int_buf, 24 * 1024 * 1024) != 0 ||
        ion_map_vpu(&hw_int_buf, &ops, &handle, 1) != 0) {
        ops.release(&handle);
        free(common_mem);
        free(nalus);
        free(file_buf);
        return 1;
    }

    memset(&cbuf, 0, sizeof(cbuf));
    cbuf.int_buffer_ptr = hw_int_buf.va;
    cbuf.int_buffer_ptr_phy = hw_int_buf.iova;
    cbuf.int_size = (uint32_t)hw_int_buf.size;
    rc = ops.mem_init(&handle, &cbuf);
    if (rc != 0) {
        fprintf(stderr, "rgslide-vpu: %s MemInit failed rc=%d\n", ops.name, rc);
        ion_free_buf(&hw_int_buf, &ops, &handle);
        ops.release(&handle);
        free(common_mem);
        free(nalus);
        free(file_buf);
        return 1;
    }

    /* Allocate ION bitstream staging buffer (4 MiB) */
    IonBuf bs_buf;
    if (ion_alloc_buf(&bs_buf, 4 * 1024 * 1024) != 0 ||
        ion_map_vpu(&bs_buf, &ops, &handle, 1) != 0) {
        ion_free_buf(&hw_int_buf, &ops, &handle);
        ops.release(&handle);
        free(common_mem);
        free(nalus);
        free(file_buf);
        return 1;
    }

    /* Allocate initial probe frame buffer (for SPS/PPS / first frame) */
    bool pool_ready = false;
    uint32_t cur_w = 0, cur_h = 0;
    uint32_t disp_w = 0, disp_h = 0;

    IonBuf init_frame;
    if (ion_alloc_buf(&init_frame, 1920 * 1088 * 3 / 2) != 0 ||
        ion_map_vpu(&init_frame, &ops, &handle, 1) != 0) {
        ion_free_buf(&bs_buf, &ops, &handle);
        ion_free_buf(&hw_int_buf, &ops, &handle);
        ops.release(&handle);
        free(common_mem);
        free(nalus);
        free(file_buf);
        return 1;
    }

    double t0 = now_sec();
    int frames_out = 0;
    int64_t pts_counter = 0;
    int au_idx = 0;

    /* Feed parameter sets (VPS/SPS/PPS) separately from VCL slices so GetInfo & DPB setup
     * always completes cleanly before the first IDR picture is decoded. */
    int i = 0;
    while (i < num_nalus) {
        if (max_frames > 0 && frames_out >= max_frames) break;

        int au_start = i;
        if (!nalus[i].is_vcl) {
            /* Feed each non-VCL NALU (VPS/SPS/PPS/SEI) individually because
             * H264DecDecode/H265DecDecode return immediately after parsing a single SPS/PPS. */
            i++;
        } else {
            /* Group one picture's VCL slice(s) */
            i++;
            while (i < num_nalus && nalus[i].is_vcl && !nalus[i].is_first_slice) i++;
        }
        int au_end = i;
        size_t au_off = nalus[au_start].offset;
        size_t au_len = (nalus[au_end - 1].offset + nalus[au_end - 1].size) - au_off;
        if (au_len == 0 || au_len > bs_buf.size - 16) continue;

        memcpy(bs_buf.va, file_buf + au_off, au_len);
        memset(bs_buf.va + au_len, 0, 16);

        FrameSlot *slot = NULL;
        if (pool_ready) {
            slot = pool_acquire_free(&pool);
            codec_set_rec(&ops, &handle, slot->buf.va, slot->buf.iova, (void *)slot, slot->id);
        } else {
            codec_set_rec(&ops, &handle, init_frame.va, init_frame.iova, (void *)0x1, 0);
        }

        MMDecInput in;
        MMDecOutput out;
        memset(&in, 0, sizeof(in));
        memset(&out, 0, sizeof(out));
        in.pStream = bs_buf.va;
        in.pStream_phy = bs_buf.iova;
        in.dataLen = (uint32_t)au_len;
        in.beLastFrm = (i >= num_nalus) ? 1 : 0;
        in.pts = pts_counter++;

        rc = ops.decode(&handle, &in, &out);
        if (verbose) {
            fprintf(stderr, "[au %d %s] len=%zu rc=%d eff=%d %ux%u picId=%u hdr=%p consumed=%u\n",
                    au_idx, nalus[au_start].is_vcl ? "VCL" : "HDR", au_len, rc,
                    out.frameEffective, out.frame_width, out.frame_height,
                    out.picId, out.pBufferHeader, in.dataLen);
        }

        if (!pool_ready) {
            DecStreamInfo info;
            memset(&info, 0, sizeof(info));
            if (ops.get_info(&handle, &info) == 0 && info.picWidth > 0 && info.picHeight > 0) {
                cur_w = info.picWidth;
                cur_h = info.picHeight;
                disp_w = (info.cropWidth > 0 && info.cropWidth <= cur_w) ? info.cropWidth : cur_w;
                disp_h = (info.cropHeight > 0 && info.cropHeight <= cur_h) ? info.cropHeight : cur_h;
                uint32_t aligned_w = (cur_w + 15u) & ~15u;
                uint32_t aligned_h = (cur_h + 15u) & ~15u;
                size_t frame_sz = (size_t)aligned_w * (size_t)aligned_h * 3 / 2 + 4096;
                int want_slots = (int)info.numRefFrames + 6;
                if (want_slots < 8) want_slots = 8;
                if (want_slots > MAX_FRAME_SLOTS) want_slots = MAX_FRAME_SLOTS;

                if (verbose) {
                    fprintf(stderr, "rgslide-vpu: stream info %ux%u (crop=%d %ux%u) refs=%u slots=%d frame_sz=%zu\n",
                            cur_w, cur_h, info.croppingFlag, info.cropWidth, info.cropHeight,
                            info.numRefFrames, want_slots, frame_sz);
                }

                for (int s = 0; s < want_slots; s++) {
                    pool.slots[s].id = (uint32_t)(s + 1);
                    pool.slots[s].ref_count = 0;
                    pool.slots[s].in_use = false;
                    if (ion_alloc_buf(&pool.slots[s].buf, frame_sz) != 0 ||
                        ion_map_vpu(&pool.slots[s].buf, &ops, &handle, 1) != 0) {
                        fprintf(stderr, "rgslide-vpu: failed allocating frame slot %d\n", s);
                        return 1;
                    }
                }
                pool.num_slots = want_slots;
                pool_ready = true;
            }
        }

        if (out.frameEffective && out.pOutFrameY) {
            cur_w = out.frame_width ? out.frame_width : cur_w;
            cur_h = out.frame_height ? out.frame_height : cur_h;
            write_nv12_frame(fout, out.pOutFrameY, cur_w, cur_h, disp_w, disp_h);
            frames_out++;
        }
        au_idx++;
    }

    /* Drain remaining buffered frames from DPB */
    for (int d = 0; d < MAX_FRAME_SLOTS; d++) {
        if (max_frames > 0 && frames_out >= max_frames) break;
        void *hdr = NULL;
        uint32_t picId = 0;
        int64_t pts = 0;
        int drc = ops.get_last(&handle, &hdr, &picId, &pts);
        if (drc != 0 || !hdr) break;
        if (hdr == (void *)0x1) {
            write_nv12_frame(fout, init_frame.va, cur_w, cur_h, disp_w, disp_h);
            frames_out++;
        } else {
            FrameSlot *s = (FrameSlot *)hdr;
            write_nv12_frame(fout, s->buf.va, cur_w, cur_h, disp_w, disp_h);
            frames_out++;
        }
    }

    double dt = now_sec() - t0;
    fprintf(stderr, "rgslide-vpu: codec=%s %ux%u (coded %ux%u) decoded=%d frames in %.3fs (%.1f FPS)\n",
            ops.name, disp_w ? disp_w : cur_w, disp_h ? disp_h : cur_h, cur_w, cur_h,
            frames_out, dt, dt > 0.0 ? (double)frames_out / dt : 0.0);

    /* Cleanup */
    if (fout && fout != stdout) fclose(fout);
    for (int s = 0; s < pool.num_slots; s++) {
        ion_free_buf(&pool.slots[s].buf, &ops, &handle);
    }
    for (int m = 0; m < pool.num_mbinfo; m++) {
        ion_free_buf(&pool.mbinfo_bufs[m], &ops, &handle);
    }
    if (pool.ext_buf_ready) {
        ion_free_buf(&pool.ext_buf, &ops, &handle);
    }
    ion_free_buf(&init_frame, &ops, &handle);
    ion_free_buf(&bs_buf, &ops, &handle);
    ion_free_buf(&hw_int_buf, &ops, &handle);
    ops.release(&handle);
    if (g_ion_fd >= 0) close(g_ion_fd);
    free(common_mem);
    free(nalus);
    free(file_buf);
    return frames_out > 0 ? 0 : 2;
}

static void usage(void) {
    fprintf(stderr,
            "Usage:\n"
            "  rgslide-vpu info\n"
            "  rgslide-vpu decode --codec h264|hevc -i <in.264|265> [-o <out.nv12|->] [--max-frames N] [-v]\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 1;
    }
    if (strcmp(argv[1], "info") == 0) {
        return cmd_info();
    }
    if (strcmp(argv[1], "decode") == 0) {
        const char *codec = "h264";
        const char *in_path = NULL;
        const char *out_path = NULL;
        int max_frames = 0;
        bool verbose = false;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--codec") == 0 && i + 1 < argc) {
                codec = argv[++i];
            } else if ((strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--input") == 0) && i + 1 < argc) {
                in_path = argv[++i];
            } else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
                out_path = argv[++i];
            } else if (strcmp(argv[i], "--max-frames") == 0 && i + 1 < argc) {
                max_frames = atoi(argv[++i]);
            } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
                verbose = true;
            }
        }
        if (!in_path) {
            usage();
            return 1;
        }
        return cmd_decode(codec, in_path, out_path, max_frames, verbose);
    }
    usage();
    return 1;
}
