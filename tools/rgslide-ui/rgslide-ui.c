/* rgslide-ui: menus de ajustes con el mando para la RG Slide (SDL2 driver hybris + GLES2, sin teclado).
 * Uso: rgslide-ui FICHERO.menu              menu declarativo (ver docs/agent-control.md)
 *      rgslide-ui --view TITULO CMD [SEG]   ejecuta CMD y muestra su salida (se repite cada SEG s si se da)
 * Idioma es|en: ver lang.h (los textos de los .menu son claves en espanol que se traducen con tr()).
 * Mando: hat/stick = mover, A = aceptar, B = volver, X = refrescar/espacio, Y = capas del teclado, L/R = pagina, Select+Start = salir. */
#include <SDL.h>
#include <SDL_opengles2.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "lang.h"

#define SW 1280
#define SH 960
#define NCH 224
#define FONT_R "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
#define FONT_B "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"

typedef struct { float x, y, u, v, r, g, b, a; } Vtx;
typedef struct { GLuint tex; stbtt_bakedchar cd[NCH]; int adv, size; } Font;
typedef struct { pid_t pid; int fd, active; char *buf; size_t len, cap; int status; } Job;
enum { T_RUN, T_LIST, T_SLIDER, T_CHOICE, T_CONFIRM };
typedef struct { int type, min, max, step, val, nopt, cur, dirty; char *label, *cmd, *get, *set; char **opt; } Item;
enum { M_MENU, M_VIEW, M_PICK, M_OSK, M_CONFIRM };
enum { A_UP = 1, A_RIGHT = 2, A_DOWN = 4, A_LEFT = 8, A_A = 16, A_B, A_X, A_Y, A_L, A_R };

static const float C_BG[4] = {0.06f, 0.07f, 0.10f, 1}, C_BAR[4] = {0.10f, 0.14f, 0.24f, 1}, C_SEL[4] = {0.15f, 0.35f, 0.65f, 1},
	C_TXT[4] = {0.93f, 0.93f, 0.93f, 1}, C_DIM[4] = {0.62f, 0.68f, 0.75f, 1}, C_OK[4] = {0.45f, 0.85f, 0.50f, 1},
	C_WARN[4] = {1.0f, 0.78f, 0.30f, 1}, C_BOX[4] = {0.11f, 0.12f, 0.17f, 1};

static SDL_Window *win;
static GLuint prog, white;
static Vtx vb[6 * 6000];
static int nv;
static GLuint curtex;
static Font fn, fb;
static int wsx = SW, wsy = SH;

static char *title = "Ajustes", *statcmd;
static Item *items;
static int nitems, sel, scroll_top;
static int mode = M_MENU, ret_mode = M_MENU;
static Job jmain, jstat, jset;
static char *stat_text = "";
static Uint32 stat_next;
static char *vtitle, *vtext = "", *vcmd;
static int vscroll, vrefresh, vwas_running;
static Uint32 vnext;
static char **pl_val, **pl_lab, **pl_pro;
static int pl_n, pl_sel, pl_top, pl_loading;
static char *pl_cmd, *pl_act, *pl_title, *pending_val;
static char osk_buf[128], *osk_prompt;
static int osk_layer, osk_r, osk_c;
static char *msg;
static Uint32 msg_until;
static int quit, viewonly;

static const char *LAYER[3][4] = {{"abcdefghij", "klmnopqrst", "uvwxyz0123", "456789-_.@"},
	{"ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ!?:;", "/\\+=*#%&$~"},
	{"!\"#$%&'()*", "+,-./:;<=>", "?@[\\]^_`{|", "}~ 0123456 "}};
static const char *OSKACT[4] = {"ESPACIO", "BORRAR", "CAPA", "ACEPTAR"};

static char *xstrdup(const char *s) { char *r = strdup(s ? s : ""); if (!r) exit(2); return r; }

static void setmsg(const char *m) { msg = xstrdup(m); msg_until = SDL_GetTicks() + 4000; }

/* ---------- procesos hijos ---------- */
static void job_kill(Job *j)
{
	if (!j->active) return;
	kill(-j->pid, SIGTERM); close(j->fd); waitpid(j->pid, &j->status, WNOHANG); j->active = 0;
}

static int job_start(Job *j, const char *cmd)
{
	int p[2];
	job_kill(j);
	if (pipe(p)) return -1;
	pid_t pid = fork();
	if (pid < 0) { close(p[0]); close(p[1]); return -1; }
	if (!pid) {
		setpgid(0, 0);
		dup2(p[1], 1); dup2(p[1], 2);
		int n = open("/dev/null", O_RDONLY); dup2(n, 0);
		for (int i = 3; i < 256; i++) close(i);
		execl("/bin/sh", "sh", "-c", cmd, (char *)0);
		_exit(127);
	}
	close(p[1]);
	fcntl(p[0], F_SETFL, O_NONBLOCK);
	j->pid = pid; j->fd = p[0]; j->active = 1; j->len = 0;
	if (!j->buf) { j->cap = 4096; j->buf = malloc(j->cap); }
	j->buf[0] = 0;
	return 0;
}

/* 1 si termino en esta llamada */
static int job_poll(Job *j)
{
	char tmp[2048];
	if (!j->active) return 0;
	for (;;) {
		ssize_t n = read(j->fd, tmp, sizeof tmp);
		if (n > 0) {
			if (j->len + n + 1 > j->cap) { j->cap = (j->len + n + 1) * 2; j->buf = realloc(j->buf, j->cap); }
			memcpy(j->buf + j->len, tmp, n); j->len += n; j->buf[j->len] = 0;
			if (j->len > (1u << 20)) { job_kill(j); return 1; }
			continue;
		}
		if (n == 0) { close(j->fd); waitpid(j->pid, &j->status, 0); j->active = 0; return 1; }
		return 0;
	}
}

static char *run_sync(const char *cmd)
{
	Job j; memset(&j, 0, sizeof j);
	if (job_start(&j, cmd)) return xstrdup("");
	Uint32 t0 = SDL_GetTicks();
	while (j.active && SDL_GetTicks() - t0 < 8000) { if (!job_poll(&j)) SDL_Delay(10); }
	if (j.active) job_kill(&j);
	char *r = xstrdup(j.buf);
	for (size_t n = strlen(r); n && (r[n - 1] == '\n' || r[n - 1] == ' '); ) r[--n] = 0;
	return r;
}

static char *subst(const char *tpl, const char *val, const char *text, int num)
{
	size_t cap = strlen(tpl) + 3 * (strlen(val ? val : "") + strlen(text ? text : "")) + 64;
	char *o = malloc(cap), *w = o;
	for (const char *s = tpl; *s; s++) {
		if (s[0] == '%' && (s[1] == 's' || s[1] == 'p')) {
			const char *v = s[1] == 's' ? val : text;
			*w++ = '\'';
			for (; v && *v; v++) { if (*v == '\'') { memcpy(w, "'\\''", 4); w += 4; } else *w++ = *v; }
			*w++ = '\''; s++;
		} else if (s[0] == '%' && s[1] == 'd') { w += sprintf(w, "%d", num); s++; }
		else *w++ = *s;
	}
	*w = 0;
	return o;
}

/* ---------- dibujo ---------- */
static void flush(void)
{
	if (!nv) return;
	glBindTexture(GL_TEXTURE_2D, curtex);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), &vb[0].x);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), &vb[0].u);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), &vb[0].r);
	glDrawArrays(GL_TRIANGLES, 0, nv);
	nv = 0;
}

static void quad(GLuint tex, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, const float *c)
{
	if (tex != curtex || nv + 6 > (int)(sizeof vb / sizeof *vb)) { flush(); curtex = tex; }
	Vtx q[4] = {{x0, y0, u0, v0, c[0], c[1], c[2], c[3]}, {x1, y0, u1, v0, c[0], c[1], c[2], c[3]},
		{x1, y1, u1, v1, c[0], c[1], c[2], c[3]}, {x0, y1, u0, v1, c[0], c[1], c[2], c[3]}};
	static const int idx[6] = {0, 1, 2, 0, 2, 3};
	for (int i = 0; i < 6; i++) vb[nv++] = q[idx[i]];
}

static void rect(float x, float y, float w, float h, const float *c) { quad(white, x, y, x + w, y + h, 0, 0, 1, 1, c); }

static int text(Font *f, float x, float top, const char *s, const float *c, int maxcols)
{
	float bx = x, by = top + f->size * 0.82f;
	int cols = 0;
	for (const unsigned char *p = (const unsigned char *)s; *p && cols < maxcols; cols++) {
		int cp = *p++;
		if (cp >= 0xC0 && cp < 0xE0 && (*p & 0xC0) == 0x80) cp = ((cp & 0x1F) << 6) | (*p++ & 0x3F);
		else if (cp >= 0x80) { cp = '?'; while ((*p & 0xC0) == 0x80) p++; }
		if (cp < 32 || cp >= 32 + NCH) cp = '?';
		stbtt_aligned_quad q;
		stbtt_GetBakedQuad(f->cd, 1024, 512, cp - 32, &bx, &by, &q, 1);
		if (cp != ' ') quad(f->tex, q.x0, q.y0, q.x1, q.y1, q.s0, q.t0, q.s1, q.t1, c);
	}
	return cols;
}

static int load_font(Font *f, const char *path, int size)
{
	FILE *fp = fopen(path, "rb");
	if (!fp) return -1;
	fseek(fp, 0, SEEK_END); long n = ftell(fp); fseek(fp, 0, SEEK_SET);
	unsigned char *ttf = malloc(n), *bm = calloc(1024 * 512, 1);
	if (fread(ttf, 1, n, fp) != (size_t)n) { fclose(fp); return -1; }
	fclose(fp);
	if (stbtt_BakeFontBitmap(ttf, 0, (float)size, bm, 1024, 512, 32, NCH, f->cd) <= 0) return -1;
	glGenTextures(1, &f->tex); glBindTexture(GL_TEXTURE_2D, f->tex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 1024, 512, 0, GL_ALPHA, GL_UNSIGNED_BYTE, bm);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	free(ttf); free(bm);
	f->size = size; f->adv = (int)(f->cd[0].xadvance + 0.5f);
	return 0;
}

static GLuint shader(GLenum t, const char *s)
{
	GLuint o = glCreateShader(t); glShaderSource(o, 1, &s, 0); glCompileShader(o);
	GLint ok; glGetShaderiv(o, GL_COMPILE_STATUS, &ok);
	if (!ok) { char l[512]; glGetShaderInfoLog(o, sizeof l, 0, l); fprintf(stderr, "shader: %s\n", l); exit(5); }
	return o;
}

static void gl_init(void)
{
	prog = glCreateProgram();
	glAttachShader(prog, shader(GL_VERTEX_SHADER, "attribute vec2 p;attribute vec2 t;attribute vec4 c;uniform vec2 res;varying vec2 vt;varying vec4 vc;"
		"void main(){gl_Position=vec4(p.x/res.x*2.-1.,1.-p.y/res.y*2.,0.,1.);vt=t;vc=c;}"));
	glAttachShader(prog, shader(GL_FRAGMENT_SHADER, "precision mediump float;uniform sampler2D tex;varying vec2 vt;varying vec4 vc;"
		"void main(){gl_FragColor=vec4(vc.rgb,vc.a*texture2D(tex,vt).a);}"));
	glBindAttribLocation(prog, 0, "p"); glBindAttribLocation(prog, 1, "t"); glBindAttribLocation(prog, 2, "c");
	glLinkProgram(prog); glUseProgram(prog);
	glUniform2f(glGetUniformLocation(prog, "res"), SW, SH);
	glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
	glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	unsigned char w1 = 255;
	glGenTextures(1, &white); glBindTexture(GL_TEXTURE_2D, white);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 1, 1, 0, GL_ALPHA, GL_UNSIGNED_BYTE, &w1);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	if (load_font(&fn, FONT_R, 30) || load_font(&fb, FONT_B, 42)) { fprintf(stderr, "rgslide-ui: no se pudo cargar la fuente DejaVu Sans Mono\n"); exit(4); }
}

static void bar(const char *t)
{
	rect(0, 0, SW, 84, C_BAR);
	text(&fb, 32, 18, t, C_TXT, 60);
}

static void footer(const char *help)
{
	rect(0, SH - 48, SW, 48, C_BAR);
	if (msg && SDL_GetTicks() < msg_until) text(&fn, 24, SH - 42, msg, C_WARN, 68);
	else text(&fn, 24, SH - 42, help, C_DIM, 68);
}

/* ---------- carga de menu ---------- */
static int split(char *s, char **f, int max)
{
	int n = 0;
	f[n++] = s;
	for (; *s && n < max; s++) if (*s == '|') { *s = 0; f[n++] = s + 1; }
	return n;
}

static void item_refresh(Item *it)
{
	if (it->type != T_SLIDER && it->type != T_CHOICE) return;
	char *v = run_sync(it->get);
	if (it->type == T_SLIDER) { it->val = atoi(v); if (it->val < it->min) it->val = it->min; if (it->val > it->max) it->val = it->max; return; }
	for (int i = 0; i < it->nopt; i++) if (!strcmp(it->opt[i], v)) { it->cur = i; return; }
	if (*v) { it->opt = realloc(it->opt, (it->nopt + 1) * sizeof *it->opt); it->opt[it->nopt] = v; it->cur = it->nopt++; }
}

static void load_menu(const char *path)
{
	FILE *fp = fopen(path, "r");
	char line[1024];
	if (!fp) { fprintf(stderr, "rgslide-ui: no puedo abrir %s\n", path); exit(3); }
	while (fgets(line, sizeof line, fp)) {
		line[strcspn(line, "\r\n")] = 0;
		if (!*line || *line == '#') continue;
		if (!strncmp(line, "title ", 6)) { title = xstrdup(tr(line + 6)); continue; }
		if (!strncmp(line, "status ", 7)) { statcmd = xstrdup(line + 7); continue; }
		char *f[8], *cp = xstrdup(line);
		int n = split(cp, f, 8);
		Item it; memset(&it, 0, sizeof it);
		if (n >= 3 && !strcmp(f[0], "run")) { it.type = T_RUN; it.label = f[1]; it.cmd = f[2]; }
		else if (n >= 3 && !strcmp(f[0], "confirm")) { it.type = T_CONFIRM; it.label = f[1]; it.cmd = f[2]; }
		else if (n >= 4 && !strcmp(f[0], "list")) { it.type = T_LIST; it.label = f[1]; it.cmd = f[2]; it.set = f[3]; }
		else if (n >= 7 && !strcmp(f[0], "slider")) { it.type = T_SLIDER; it.label = f[1]; it.min = atoi(f[2]); it.max = atoi(f[3]); it.step = atoi(f[4]); it.get = f[5]; it.set = f[6]; }
		else if (n >= 5 && !strcmp(f[0], "choice")) {
			it.type = T_CHOICE; it.label = f[1]; it.get = f[2]; it.set = f[3];
			for (char *o = strtok(f[4], ","); o; o = strtok(0, ",")) { it.opt = realloc(it.opt, (it.nopt + 1) * sizeof *it.opt); it.opt[it.nopt++] = o; }
		} else { fprintf(stderr, "rgslide-ui: linea ignorada: %s\n", line); continue; }
		it.label = (char *)tr(it.label);
		items = realloc(items, (nitems + 1) * sizeof *items);
		items[nitems++] = it;
	}
	fclose(fp);
}

/* ---------- acciones ---------- */
static void view_open(const char *t, const char *cmd, int refresh, int ret)
{
	vtitle = xstrdup(t); vcmd = xstrdup(cmd); vrefresh = refresh; ret_mode = ret;
	vtext = ""; vscroll = 0; mode = M_VIEW; vwas_running = 1;
	job_start(&jmain, cmd);
}

static void pick_load(void)
{
	pl_loading = 1; pl_n = 0;
	job_start(&jmain, pl_cmd);
}

static void pick_parse(char *out)
{
	pl_n = 0; pl_sel = pl_top = 0;
	free(pl_val); free(pl_lab); free(pl_pro); pl_val = pl_lab = pl_pro = 0;
	for (char *s = out; s && *s; ) {
		char *e = strchr(s, '\n'); if (e) *e = 0;
		if (*s) {
			char *tabs[3]; int k = 0; char *c = xstrdup(s);
			tabs[k++] = c;
			for (char *t = c; *t && k < 3; t++) if (*t == '\t') { *t = 0; tabs[k++] = t + 1; }
			pl_val = realloc(pl_val, (pl_n + 1) * sizeof *pl_val); pl_lab = realloc(pl_lab, (pl_n + 1) * sizeof *pl_lab); pl_pro = realloc(pl_pro, (pl_n + 1) * sizeof *pl_pro);
			pl_val[pl_n] = tabs[0]; pl_lab[pl_n] = k > 1 ? tabs[1] : tabs[0]; pl_pro[pl_n] = k > 2 ? tabs[2] : (char *)"";
			pl_n++;
		}
		s = e ? e + 1 : 0;
	}
}

static void osk_open(const char *prompt)
{
	osk_buf[0] = 0; osk_prompt = xstrdup(prompt); osk_layer = osk_r = osk_c = 0; mode = M_OSK;
}

static void act_menu(int a)
{
	Item *it = &items[sel];
	switch (a) {
	case A_UP: if (nitems) sel = (sel + nitems - 1) % nitems; break;
	case A_DOWN: if (nitems) sel = (sel + 1) % nitems; break;
	case A_LEFT: case A_RIGHT: {
		int d = a == A_RIGHT ? 1 : -1;
		if (it->type == T_SLIDER) { int v = it->val + d * it->step; if (v < it->min) v = it->min; if (v > it->max) v = it->max; if (v != it->val) { it->val = v; it->dirty = 1; } }
		else if (it->type == T_CHOICE && it->nopt) { it->cur = (it->cur + it->nopt + d) % it->nopt; it->dirty = 1; }
		break; }
	case A_A:
		if (it->type == T_RUN) view_open(it->label, it->cmd, 0, M_MENU);
		else if (it->type == T_CONFIRM) mode = M_CONFIRM;
		else if (it->type == T_LIST) { pl_cmd = it->cmd; pl_act = it->set; pl_title = it->label; pl_n = 0; mode = M_PICK; pick_load(); }
		break;
	case A_B: quit = 1; break;
	}
}

static void act_view(int a)
{
	int rows = (SH - 84 - 48 - 20) / 36;
	switch (a) {
	case A_UP: if (vscroll > 0) vscroll--; break;
	case A_DOWN: vscroll++; break;
	case A_LEFT: case A_L: vscroll = vscroll > rows ? vscroll - rows : 0; break;
	case A_RIGHT: case A_R: vscroll += rows; break;
	case A_Y: case A_X: if (!jmain.active) { vwas_running = 1; job_start(&jmain, vcmd); } break;
	case A_A: case A_B: job_kill(&jmain); if (viewonly) quit = 1; else mode = ret_mode; break;
	}
}

static void act_pick(int a)
{
	switch (a) {
	case A_UP: if (pl_n) pl_sel = (pl_sel + pl_n - 1) % pl_n; break;
	case A_DOWN: if (pl_n) pl_sel = (pl_sel + 1) % pl_n; break;
	case A_X: if (!pl_loading) pick_load(); break;
	case A_B: job_kill(&jmain); pl_loading = 0; mode = M_MENU; break;
	case A_A:
		if (pl_loading || !pl_n) break;
		pending_val = pl_val[pl_sel];
		if (*pl_pro[pl_sel]) osk_open(pl_pro[pl_sel]);
		else { view_open(pl_lab[pl_sel], subst(pl_act, pending_val, "", 0), 0, M_MENU); }
		break;
	}
}

static void act_confirm(int a)
{
	if (a == A_A) view_open(items[sel].label, items[sel].cmd, 0, M_MENU);
	else if (a == A_B) mode = M_MENU;
}

static void act_osk(int a)
{
	size_t n = strlen(osk_buf);
	switch (a) {
	case A_UP: osk_r = (osk_r + 4) % 5; break;
	case A_DOWN: osk_r = (osk_r + 1) % 5; break;
	case A_LEFT: osk_c = (osk_c + (osk_r == 4 ? 3 : 9)) % (osk_r == 4 ? 4 : 10); break;
	case A_RIGHT: osk_c = (osk_c + 1) % (osk_r == 4 ? 4 : 10); break;
	case A_X: if (n < sizeof osk_buf - 1) { osk_buf[n] = ' '; osk_buf[n + 1] = 0; } break;
	case A_B: if (n) osk_buf[n - 1] = 0; else mode = M_PICK; break;
	case A_Y: osk_layer = (osk_layer + 1) % 3; break;
	case A_L: osk_layer = (osk_layer + 2) % 3; break;
	case A_R: osk_layer = (osk_layer + 1) % 3; break;
	case A_A:
		if (osk_r < 4) { if (n < sizeof osk_buf - 1) { osk_buf[n] = LAYER[osk_layer][osk_r][osk_c]; osk_buf[n + 1] = 0; } }
		else if (osk_c == 0) { if (n < sizeof osk_buf - 1) { osk_buf[n] = ' '; osk_buf[n + 1] = 0; } }
		else if (osk_c == 1) { if (n) osk_buf[n - 1] = 0; }
		else if (osk_c == 2) osk_layer = (osk_layer + 1) % 3;
		else view_open(pl_lab[pl_sel], subst(pl_act, pending_val, osk_buf, 0), 0, M_MENU);
		break;
	}
	if (mode == M_OSK && osk_r == 4 && osk_c > 3) osk_c = 3;
}

static void handle(int a)
{
	switch (mode) {
	case M_MENU: act_menu(a); break;
	case M_VIEW: act_view(a); break;
	case M_PICK: act_pick(a); break;
	case M_OSK: act_osk(a); break;
	case M_CONFIRM: act_confirm(a); break;
	}
}

/* ---------- pantallas ---------- */
static void draw_status(int *y)
{
	if (!statcmd) return;
	int lines = 0;
	for (const char *s = stat_text; *s; s++) if (*s == '\n') lines++;
	if (*stat_text && stat_text[strlen(stat_text) - 1] != '\n') lines++;
	if (lines > 9) lines = 9;
	if (!lines) lines = 1;
	rect(20, *y, SW - 40, lines * 34 + 16, C_BOX);
	char *cp = xstrdup(stat_text), *s = cp;
	int row = 0;
	if (!*cp) text(&fn, 36, *y + 8, tr("Leyendo estado..."), C_DIM, 64);
	while (*s && row < lines) {
		char *e = strchr(s, '\n'); if (e) *e = 0;
		text(&fn, 36, *y + 8 + row * 34, s, C_OK, 64);
		row++; s = e ? e + 1 : s + strlen(s);
	}
	*y += lines * 34 + 32;
}

static void draw_menu(void)
{
	bar(title);
	int y = 100;
	draw_status(&y);
	int rows = (SH - 48 - y - 8) / 56;
	if (sel < scroll_top) scroll_top = sel;
	if (sel >= scroll_top + rows) scroll_top = sel - rows + 1;
	for (int i = scroll_top; i < nitems && i < scroll_top + rows; i++) {
		Item *it = &items[i];
		float yy = y + (i - scroll_top) * 56;
		if (i == sel) rect(20, yy, SW - 40, 50, C_SEL);
		text(&fn, 40, yy + 9, it->label, C_TXT, (it->type == T_SLIDER || it->type == T_CHOICE) ? 33 : 62);
		if (it->type == T_SLIDER) {
			float frac = (float)(it->val - it->min) / (it->max - it->min ? it->max - it->min : 1);
			rect(640, yy + 17, 400, 16, C_BOX); rect(640, yy + 17, 400 * frac, 16, i == sel ? C_OK : C_DIM);
			char b[16]; snprintf(b, sizeof b, "%d", it->val); text(&fn, 1060, yy + 9, b, C_TXT, 6);
		} else if (it->type == T_CHOICE && it->nopt) {
			char b[96]; snprintf(b, sizeof b, "< %s >", tr(it->opt[it->cur])); text(&fn, 640, yy + 9, b, i == sel ? C_OK : C_DIM, 30);
		} else if (it->type == T_LIST) text(&fn, 1180, yy + 9, ">", C_DIM, 2);
	}
	Item *it = nitems ? &items[sel] : 0;
	footer(tr(it && (it->type == T_SLIDER || it->type == T_CHOICE) ? "Izq/Der: cambiar    B: salir" : "A: aceptar    B: salir"));
}

static void draw_view(void)
{
	bar(vtitle);
	int cols = (SW - 60) / fn.adv, rows = (SH - 84 - 48 - 20) / 36;
	char *cp = xstrdup(vtext);
	char **w = malloc(sizeof *w * 4100); size_t *wl = malloc(sizeof *wl * 4100);
	int nw = 0;
	for (char *p = cp; p && nw < 4000; ) {
		char *e = strchr(p, '\n'); size_t L = e ? (size_t)(e - p) : strlen(p);
		if (!L) { w[nw] = p; wl[nw++] = 0; }
		for (size_t o = 0; o < L && nw < 4000; o += cols) { w[nw] = p + o; wl[nw++] = L - o > (size_t)cols ? (size_t)cols : L - o; }
		p = e ? e + 1 : 0;
		if (p && !*p) break;
	}
	if (vscroll > nw - rows) vscroll = nw > rows ? nw - rows : 0;
	if (vscroll < 0) vscroll = 0;
	for (int i = 0; i < rows && vscroll + i < nw; i++) {
		char b[512]; size_t L = wl[vscroll + i] < sizeof b - 1 ? wl[vscroll + i] : sizeof b - 1;
		memcpy(b, w[vscroll + i], L); b[L] = 0;
		text(&fn, 30, 96 + i * 36, b, C_TXT, cols);
	}
	if (nw > rows) { float h = (SH - 84 - 48) * (float)rows / nw; rect(SW - 10, 84 + (SH - 84 - 48 - h) * vscroll / (nw - rows), 6, h, C_DIM); }
	free(w); free(wl); free(cp);
	static const char *spin[] = {"|", "/", "-", "\\"};
	char h[128];
	snprintf(h, sizeof h, "%s  %s", jmain.active ? spin[(SDL_GetTicks() / 150) % 4] : " ", tr("Arriba/Abajo: desplazar   L/R: pagina   Y: repetir   A/B: volver"));
	footer(h);
	if (jmain.active && !*vtext) text(&fn, 30, 100, tr("Ejecutando..."), C_WARN, 30);
}

static void draw_pick(void)
{
	bar(pl_title);
	int rows = (SH - 84 - 48 - 16) / 52;
	if (pl_loading) text(&fn, 40, 110, tr("Buscando..."), C_WARN, 30);
	else if (!pl_n) text(&fn, 40, 110, tr("(sin resultados)"), C_DIM, 30);
	if (pl_sel < pl_top) pl_top = pl_sel;
	if (pl_sel >= pl_top + rows) pl_top = pl_sel - rows + 1;
	for (int i = pl_top; i < pl_n && i < pl_top + rows; i++) {
		float yy = 96 + (i - pl_top) * 52;
		if (i == pl_sel) rect(20, yy, SW - 40, 48, C_SEL);
		text(&fn, 40, yy + 8, pl_lab[i], C_TXT, 66);
	}
	footer(tr("Arriba/Abajo: elegir   A: aceptar   X: buscar de nuevo   B: volver"));
}

static void draw_osk(void)
{
	bar(osk_prompt);
	rect(40, 110, SW - 80, 70, C_BOX);
	char b[160]; snprintf(b, sizeof b, "%s%s", osk_buf, (SDL_GetTicks() / 500) % 2 ? "_" : " ");
	text(&fb, 60, 122, b, C_OK, 40);
	int cw = 110, ch = 96, x0 = (SW - 10 * cw) / 2, y0 = 220;
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 10; c++) {
			float x = x0 + c * cw, y = y0 + r * ch;
			rect(x + 4, y + 4, cw - 8, ch - 8, r == osk_r && c == osk_c ? C_SEL : C_BOX);
			char k[2] = {LAYER[osk_layer][r][c], 0};
			text(&fb, x + cw / 2 - fb.adv / 2, y + 22, k, C_TXT, 1);
		}
	for (int c = 0; c < 4; c++) {
		float x = x0 + c * 270, y = y0 + 4 * ch + 10;
		rect(x + 4, y + 4, 262, ch - 8, osk_r == 4 && osk_c == c ? C_SEL : C_BOX);
		const char *oa = tr(OSKACT[c]);
		text(&fn, x + 131 - lang_cols(oa) * fn.adv / 2, y + 30, oa, C_TXT, 12);
	}
	footer(tr("A: escribir   B: borrar   X: espacio   Y: mayus/simbolos"));
}

static void draw_confirm(void)
{
	draw_menu();
	rect(0, 0, SW, SH, (float[]){0, 0, 0, 0.6f});
	rect(240, 330, 800, 260, C_BAR);
	text(&fb, 280, 360, items[sel].label, C_TXT, 36);
	text(&fn, 280, 450, tr("Seguro?"), C_WARN, 20);
	text(&fn, 280, 520, tr("A: si, continuar        B: no, volver"), C_TXT, 50);
}

static void render(void)
{
	glViewport(0, 0, wsx, wsy);
	glClearColor(C_BG[0], C_BG[1], C_BG[2], 1); glClear(GL_COLOR_BUFFER_BIT);
	switch (mode) {
	case M_MENU: draw_menu(); break;
	case M_VIEW: draw_view(); break;
	case M_PICK: draw_pick(); break;
	case M_OSK: draw_osk(); break;
	case M_CONFIRM: draw_confirm(); break;
	}
	flush();
	SDL_GL_SwapWindow(win);
}

/* ---------- bucle ---------- */
static void tick(void)
{
	Uint32 now = SDL_GetTicks();
	if (job_poll(&jmain)) {
		if (mode == M_VIEW) {
			vtext = xstrdup(jmain.buf);
			if (WIFEXITED(jmain.status) && WEXITSTATUS(jmain.status)) { char b[96], c[64]; snprintf(c, sizeof c, tr("[codigo de salida %d]"), WEXITSTATUS(jmain.status)); snprintf(b, sizeof b, "\n%s\n", c); vtext = realloc(vtext, strlen(vtext) + strlen(b) + 1); strcat(vtext, b); }
			vwas_running = 0; vnext = now + vrefresh * 1000;
		} else if (mode == M_PICK) { pl_loading = 0; pick_parse(jmain.buf); }
	}
	if (mode == M_VIEW && vrefresh && !jmain.active && now >= vnext) job_start(&jmain, vcmd);
	if (statcmd) {
		if (job_poll(&jstat)) { stat_text = xstrdup(jstat.buf); stat_next = now + 4000; }
		if (!jstat.active && now >= stat_next && mode == M_MENU) job_start(&jstat, statcmd);
	}
	if (job_poll(&jset)) {
		if (WIFEXITED(jset.status) && WEXITSTATUS(jset.status)) { char *e = jset.buf; size_t n = strlen(e); while (n && e[n - 1] == '\n') e[--n] = 0; char *l = strrchr(e, '\n'); setmsg(l ? l + 1 : e); }
	}
	if (!jset.active) for (int i = 0; i < nitems; i++) if (items[i].dirty) {
		Item *it = &items[i]; it->dirty = 0;
		char *c = it->type == T_SLIDER ? subst(it->set, "", "", it->val) : subst(it->set, it->opt[it->cur], "", 0);
		job_start(&jset, c);
		break;
	}
}

int main(int argc, char **argv)
{
	lang_init();
	if (argc < 2) { fprintf(stderr, "uso: rgslide-ui FICHERO.menu | --view TITULO CMD [SEG]\n"); return 1; }
	if (!strcmp(argv[1], "--view") && argc >= 4) {
		viewonly = 1;
	} else load_menu(argv[1]);
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) < 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 2; }
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	win = SDL_CreateWindow("rgslide-ui", 0, 0, SW, SH, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
	if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 3; }
	SDL_GLContext ctx = SDL_GL_CreateContext(win);
	if (!ctx) { fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError()); return 4; }
	SDL_GL_SetSwapInterval(1);
	SDL_GetWindowSize(win, &wsx, &wsy);
	for (int i = 0; i < SDL_NumJoysticks(); i++) SDL_JoystickOpen(i);
	gl_init();
	signal(SIGPIPE, SIG_IGN);
	if (viewonly) view_open(tr(argv[2]), argv[3], argc > 4 ? atoi(argv[4]) : 0, M_VIEW);
	else { text(&fb, 32, 400, tr("Cargando..."), C_TXT, 20); flush(); SDL_GL_SwapWindow(win); for (int i = 0; i < nitems; i++) item_refresh(&items[i]); }
	int hat = 0, axm = 0, prev = 0, selb = 0, stab = 0;
	Uint32 next_rep = 0;
	while (!quit) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			switch (e.type) {
			case SDL_QUIT: quit = 1; break;
			case SDL_JOYHATMOTION: hat = e.jhat.value; break;
			case SDL_JOYAXISMOTION:
				if (e.jaxis.axis == 0 || e.jaxis.axis == 1) {
					int bit = e.jaxis.axis == 0 ? A_LEFT | A_RIGHT : A_UP | A_DOWN;
					axm &= ~bit;
					if (e.jaxis.value > 16000) axm |= e.jaxis.axis == 0 ? A_RIGHT : A_DOWN;
					if (e.jaxis.value < -16000) axm |= e.jaxis.axis == 0 ? A_LEFT : A_UP;
				}
				break;
			case SDL_JOYBUTTONDOWN:
				switch (e.jbutton.button) {
				case 0: handle(A_A); break; case 1: handle(A_B); break; case 3: handle(A_X); break; case 4: handle(A_Y); break;
				case 6: handle(A_L); break; case 7: handle(A_R); break;
				case 10: selb = 1; break; case 11: stab = 1; break;
				}
				if (selb && stab) quit = 1;
				break;
			case SDL_JOYBUTTONUP: if (e.jbutton.button == 10) selb = 0; if (e.jbutton.button == 11) stab = 0; break;
			case SDL_KEYDOWN:
				switch (e.key.keysym.sym) {
				case SDLK_UP: handle(A_UP); break; case SDLK_DOWN: handle(A_DOWN); break; case SDLK_LEFT: handle(A_LEFT); break; case SDLK_RIGHT: handle(A_RIGHT); break;
				case SDLK_RETURN: handle(A_A); break; case SDLK_ESCAPE: handle(A_B); break;
				}
				break;
			}
		}
		int cur = (hat & 15) | axm;
		Uint32 now = SDL_GetTicks();
		for (int b = 1; b <= 8; b <<= 1) if ((cur & b) && !(prev & b)) { handle(b); next_rep = now + 350; }
		if (cur && cur == prev && now >= next_rep) { for (int b = 1; b <= 8; b <<= 1) if (cur & b) handle(b); next_rep = now + 80; }
		prev = cur;
		tick();
		render();
		SDL_Delay(8);
	}
	job_kill(&jmain); job_kill(&jstat); job_kill(&jset);
	SDL_GL_DeleteContext(ctx); SDL_DestroyWindow(win); SDL_Quit();
	return 0;
}
