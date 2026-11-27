/* rgslide-python3: lanzador mínimo de Python 3.13 para la Anbernic RG Slide (Mali + libhybris).
 *
 * En Debian arm64, /usr/bin/python3.13 enlaza libpython estáticamente y coloca _Py_tss_tstate
 * en el bloque TLS del ejecutable principal (TPIDR_EL0 + 0x18). El blob de Mali de Android
 * usa ese mismo offset (TLS_SLOT_OPENGL = 3) para su contexto EGL/GL, pisando _Py_tss_tstate
 * y provocando "Fatal Python error: _PyThreadState_Attach: non-NULL old thread state".
 *
 * Este binario reserva los primeros 512 bytes del TLS estático del ejecutable principal
 * (tp+0x10..tp+0x210) y carga libpython3.13.so.1.0 dinámicamente vía dlopen(RTLD_GLOBAL),
 * desplazando _Py_tss_tstate fuera del área de Bionic/Mali sin hooks ni penalización.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

__thread char rgslide_tls_pad[512] __attribute__((aligned(16), used));

typedef int (*py_bytes_main_fn)(int, char **);

int main(int argc, char **argv)
{
    /* Asegura que el enlazador conserve PT_TLS en el ejecutable principal. */
    volatile char *pad = rgslide_tls_pad;
    pad[0] = 0;

    void *h = dlopen("libpython3.13.so.1.0", RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        fprintf(stderr, "rgslide-python3: dlopen(libpython3.13.so.1.0): %s\n", dlerror());
        return 127;
    }

    py_bytes_main_fn py_bytes_main = (py_bytes_main_fn)dlsym(h, "Py_BytesMain");
    if (!py_bytes_main) {
        fprintf(stderr, "rgslide-python3: dlsym(Py_BytesMain): %s\n", dlerror());
        return 127;
    }

    return py_bytes_main(argc, argv);
}
