/* esshot: LD_PRELOAD para EmulationStation (SDL2 hybris). Intercepta SDL_GL_SwapWindow: antes de presentar lee el framebuffer con glReadPixels
 * y lo guarda como PPM, y ademas inyecta pulsaciones del mando. Guion: si existe /tmp/es-script se lee entero (y se borra) y se ejecuta una linea por paso:
 *   wait MS | shot RUTA.ppm | btn N | hat V (1=arriba 2=derecha 4=abajo 8=izquierda)
 * Cargarlo DESPUES de libtlspad (LD_PRELOAD=libtlspad.so:esshot.so). Solo para diagnostico. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <SDL.h>

typedef void (*readpix_t)(int, int, int, int, unsigned, unsigned, void *);
static char *lines[256]; static int nlines, cur;
static unsigned wait_until, rel_at; static int rel_kind, rel_val;

static void shot(SDL_Window *w, const char *path) {
    int W, H; SDL_GL_GetDrawableSize(w, &W, &H);
    readpix_t rp = (readpix_t)dlsym(RTLD_DEFAULT, "glReadPixels");
    if (!rp) { fprintf(stderr, "esshot: sin glReadPixels\n"); return; }
    unsigned char *px = malloc((size_t)W * H * 4);
    rp(0, 0, W, H, 0x1908 /*GL_RGBA*/, 0x1401 /*GL_UNSIGNED_BYTE*/, px);
    FILE *f = fopen(path, "wb");
    if (!f) { free(px); return; }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = H - 1; y >= 0; y--)
        for (int x = 0; x < W; x++) fwrite(px + ((size_t)y * W + x) * 4, 1, 3, f);
    fclose(f); free(px);
    fprintf(stderr, "esshot: %s %dx%d\n", path, W, H);
}

static void push(int type, int a, int b) {
    SDL_Event e; memset(&e, 0, sizeof e); e.type = type;
    if (type == SDL_JOYHATMOTION) { e.jhat.which = 0; e.jhat.hat = 0; e.jhat.value = b; }
    else { e.jbutton.which = 0; e.jbutton.button = a; e.jbutton.state = type == SDL_JOYBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED; }
    SDL_PushEvent(&e);
}

void SDL_GL_SwapWindow(SDL_Window *w) {
    static void (*real)(SDL_Window *);
    if (!real) real = dlsym(RTLD_NEXT, "SDL_GL_SwapWindow");
    unsigned now = SDL_GetTicks();
    if (cur >= nlines) {
        FILE *f = fopen("/tmp/es-script", "r");
        if (f) {
            char buf[512]; nlines = cur = 0;
            while (nlines < 256 && fgets(buf, sizeof buf, f)) { buf[strcspn(buf, "\n")] = 0; if (*buf) lines[nlines++] = strdup(buf); }
            fclose(f); unlink("/tmp/es-script");
        }
    }
    if (rel_at && now >= rel_at) { if (rel_kind == 0) push(SDL_JOYBUTTONUP, rel_val, 0); else push(SDL_JOYHATMOTION, 0, 0); rel_at = 0; }
    if (!rel_at && cur < nlines && now >= wait_until) {
        char *l = lines[cur++]; char a[400]; int n;
        if (sscanf(l, "wait %d", &n) == 1) wait_until = now + n;
        else if (sscanf(l, "shot %399s", a) == 1) shot(w, a);
        else if (sscanf(l, "btn %d", &n) == 1) { push(SDL_JOYBUTTONDOWN, n, 0); rel_kind = 0; rel_val = n; rel_at = now + 120; wait_until = now + 250; }
        else if (sscanf(l, "hat %d", &n) == 1) { push(SDL_JOYHATMOTION, 0, n); rel_kind = 1; rel_at = now + 120; wait_until = now + 250; }
    }
    real(w);
}
