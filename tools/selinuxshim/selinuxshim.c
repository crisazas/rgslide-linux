/* libselinux minima para correr servicemanager/hwservicemanager/vndservicemanager de Android en Debian (sin SELinux).
 * Permite todo: selinux_check_access devuelve 0; getcon/getpidcon dan un contexto fijo; selabel_lookup devuelve el de servicio.
 * No lleva libc: se compila con -nostdlib. Enlazar como libselinux.so (SONAME) y colocar en /opt/android/system/lib64 sustituyendo el real. */
typedef unsigned long size_t;
typedef int pid_t;

struct selabel_handle { int dummy; };
static struct selabel_handle H;
static char CTX[] = "u:r:shell:s0";
static char *dup_ctx(char **c) { *c = CTX; return CTX; }

union selinux_callback { int (*p)(int, const char *, ...); void *v; };

int is_selinux_enabled(void) { return 0; }
int is_selinux_mls_enabled(void) { return 0; }
int security_getenforce(void) { return 0; }
int security_policyvers(void) { return 33; }
int getcon(char **c) { dup_ctx(c); return 0; }
int getpidcon(pid_t p, char **c) { (void)p; dup_ctx(c); return 0; }
int getpeercon(int fd, char **c) { (void)fd; dup_ctx(c); return 0; }
int getfilecon(const char *p, char **c) { (void)p; dup_ctx(c); return 0; }
void freecon(char *c) { (void)c; }
int selinux_check_access(const char *s, const char *t, const char *cl, const char *perm, void *aux) { (void)s; (void)t; (void)cl; (void)perm; (void)aux; return 0; }
void selinux_set_callback(int type, union selinux_callback cb) { (void)type; (void)cb; }
union selinux_callback selinux_get_callback(int type) { union selinux_callback c; (void)type; c.v = 0; return c; }
int selinux_status_open(int fallback) { (void)fallback; return 0; }
int selinux_status_updated(void) { return 0; }
void selinux_status_close(void) {}
struct selabel_handle *selinux_android_service_context_handle(void) { return &H; }
struct selabel_handle *selinux_android_hw_service_context_handle(void) { return &H; }
struct selabel_handle *selinux_android_vendor_service_context_handle(void) { return &H; }
struct selabel_handle *selinux_android_file_context_handle(void) { return &H; }
struct selabel_handle *selabel_open(unsigned int backend, const void *opts, unsigned nopts) { (void)backend; (void)opts; (void)nopts; return &H; }
void selabel_close(struct selabel_handle *h) { (void)h; }
int selabel_lookup(struct selabel_handle *h, char **con, const char *key, int type) { (void)h; (void)key; (void)type; dup_ctx(con); return 0; }
int selabel_lookup_raw(struct selabel_handle *h, char **con, const char *key, int type) { return selabel_lookup(h, con, key, type); }
int selinux_vendor_log_callback(int type, const char *fmt, ...) { (void)type; (void)fmt; return 0; }
int selinux_log_callback(int type, const char *fmt, ...) { (void)type; (void)fmt; return 0; }
int setcon(const char *c) { (void)c; return 0; }
int setexeccon(const char *c) { (void)c; return 0; }
int setfscreatecon(const char *c) { (void)c; return 0; }
int restorecon(const char *p, int f) { (void)p; (void)f; return 0; }
int selinux_android_restorecon(const char *p, unsigned f) { (void)p; (void)f; return 0; }
