/* touchtest: prueba interactiva del tactil (fts_ts) sobre SDL2-hybris + GLES2. Lee el evdev directamente (protocolo MT tipo B), no depende de SDL_FINGER*.
 * Fases: 1) tocar 9 puntos, 2) seguir una linea en zigzag, 3) dibujo libre con varios dedos. Verde = bien.
 * Uso: touchtest [segundos_totales=150].  Salir: Start+Select (botones 10 y 11) o el tiempo. Boton 0 = saltar fase.
 * Resultado: stdout y /tmp/touchtest.log. Con la pantalla solo puede haber un proceso: parar rgslide-es antes (scripts/run-touchtest.sh). */
#define _GNU_SOURCE
#include <SDL.h>
#include <SDL_opengles2.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define NS 10
#define LOGD(...) do { printf(__VA_ARGS__); fflush(stdout); } while (0)
typedef struct { int act, isnew, isup, dirty, id; float x, y; } Slot;
typedef struct { int type, slot; float x, y; } Ev;   /* 0 down, 1 move, 2 up; x,y en pixeles de pantalla */

static Slot sl[NS]; static int cs, tfd = -1, axmax = 1279, aymax = 959, W = 1280, H = 960;
static Ev evq[512]; static int nev;
static int touch_open(char *name, size_t n)
{
	DIR *d = opendir("/dev/input"); struct dirent *e;
	while (d && (e = readdir(d))) {
		if (strncmp(e->d_name, "event", 5)) continue;
		char p[64]; snprintf(p, sizeof p, "/dev/input/%s", e->d_name);
		int fd = open(p, O_RDONLY | O_NONBLOCK); if (fd < 0) continue;
		char nm[96] = ""; ioctl(fd, EVIOCGNAME(sizeof nm), nm);
		if (!strcmp(nm, "fts_ts")) {
			struct input_absinfo a;
			if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &a) == 0) axmax = a.maximum;
			if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &a) == 0) aymax = a.maximum;
			snprintf(name, n, "%s (%s) max %dx%d", nm, p, axmax, aymax); closedir(d); return fd;
		}
		close(fd);
	}
	if (d) closedir(d);
	return -1;
}
static void push(int t, int s) { if (nev < 512) evq[nev++] = (Ev){ t, s, sl[s].x * (W - 1) / axmax, sl[s].y * (H - 1) / aymax }; }
static void touch_poll(void)
{
	struct input_event ie; nev = 0;
	while (tfd >= 0 && read(tfd, &ie, sizeof ie) == sizeof ie) {
		if (ie.type == EV_ABS) {
			if (ie.code == ABS_MT_SLOT) cs = ie.value < 0 ? 0 : ie.value >= NS ? NS - 1 : ie.value;
			else if (ie.code == ABS_MT_TRACKING_ID) { if (ie.value >= 0) { sl[cs].act = 1; sl[cs].isnew = 1; sl[cs].id = ie.value; } else if (sl[cs].act) { sl[cs].act = 0; sl[cs].isup = 1; } }
			else if (ie.code == ABS_MT_POSITION_X) { sl[cs].x = ie.value; sl[cs].dirty = 1; }
			else if (ie.code == ABS_MT_POSITION_Y) { sl[cs].y = ie.value; sl[cs].dirty = 1; }
		} else if (ie.type == EV_SYN && ie.code == SYN_REPORT) {
			for (int s = 0; s < NS; s++) {
				if (sl[s].isnew) { push(0, s); sl[s].isnew = 0; sl[s].dirty = 0; }
				else if (sl[s].dirty && sl[s].act) { push(1, s); sl[s].dirty = 0; }
				if (sl[s].isup) { push(2, s); sl[s].isup = 0; }
			}
		}
	}
}

static GLuint sh(GLenum t, const char *s) { GLuint o = glCreateShader(t); glShaderSource(o, 1, &s, 0); glCompileShader(o); return o; }
static GLuint prog(const char *vs, const char *fs) { GLuint p = glCreateProgram(); glAttachShader(p, sh(GL_VERTEX_SHADER, vs)); glAttachShader(p, sh(GL_FRAGMENT_SHADER, fs)); return p; }
static GLuint pq, pp; static GLint uc, uh, ua, ur, uk, ucirc;
static void rect(float cx, float cy, float hw, float hh, float ang, int circ, float r, float g, float b)
{
	static const GLfloat v[] = { -1,-1, 1,-1, -1,1, 1,1 };
	glUseProgram(pq); glBindAttribLocation(pq, 0, "p");
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, v); glEnableVertexAttribArray(0);
	glUniform2f(uc, cx, cy); glUniform2f(uh, hw, hh); glUniform1f(ua, ang); glUniform2f(ur, W, H); glUniform3f(uk, r, g, b); glUniform1f(ucirc, circ);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
static void seg(float x0, float y0, float x1, float y1, float th, float r, float g, float b)
{
	float dx = x1 - x0, dy = y1 - y0; rect((x0 + x1) / 2, (y0 + y1) / 2, hypotf(dx, dy) / 2, th / 2, atan2f(dy, dx), 0, r, g, b);
}
static const float SC[NS][3] = { {1,.9,.2},{.2,.9,1},{1,.4,.8},{.5,1,.4},{1,.6,.2},{.7,.6,1},{1,1,1},{.4,.8,.6},{.9,.5,.5},{.6,.9,.9} };

#define NPATH 6
static float PX[NPATH], PY[NPATH], PL[NPATH];
static float dist_seg(float px, float py, int i, float *t)
{
	float ax = PX[i], ay = PY[i], bx = PX[i + 1], by = PY[i + 1], dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
	float u = ((px - ax) * dx + (py - ay) * dy) / l2; if (u < 0) u = 0; if (u > 1) u = 1; *t = u;
	return hypotf(px - (ax + u * dx), py - (ay + u * dy));
}

int main(int argc, char **argv)
{
	int total = argc > 1 ? atoi(argv[1]) : 150;
	FILE *lg = fopen("/tmp/touchtest.log", "w");
#define LOG(...) do { printf(__VA_ARGS__); fflush(stdout); if (lg) { fprintf(lg, __VA_ARGS__); fflush(lg); } } while (0)
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) < 0) { LOG("FALLO SDL_Init: %s\n", SDL_GetError()); return 2; }
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2); SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_Window *w = SDL_CreateWindow("touchtest", 0, 0, 1280, 960, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
	if (!w) { LOG("FALLO ventana: %s\n", SDL_GetError()); return 3; }
	SDL_GLContext c = SDL_GL_CreateContext(w); if (!c) { LOG("FALLO contexto: %s\n", SDL_GetError()); return 4; }
	SDL_GetWindowSize(w, &W, &H);
	char tn[160] = ""; tfd = touch_open(tn, sizeof tn);
	LOG("pantalla %dx%d; tactil: %s\n", W, H, tfd >= 0 ? tn : "NO ENCONTRADO (fts_ts)");
	LOG("SDL ve %d dispositivos tactiles\n", SDL_GetNumTouchDevices());
	for (int i = 0; i < SDL_NumJoysticks(); i++) SDL_JoystickOpen(i);
	if (tfd < 0) { SDL_Quit(); return 5; }

	pq = prog("attribute vec2 p; uniform vec2 c,h,r; uniform float a; varying vec2 v; void main(){ v=p; float co=cos(a),si=sin(a); vec2 q=vec2(co*p.x*h.x-si*p.y*h.y, si*p.x*h.x+co*p.y*h.y)+c; gl_Position=vec4(q.x/r.x*2.-1.,1.-q.y/r.y*2.,0.,1.); }",
		"precision mediump float; uniform vec3 k; uniform float circ; varying vec2 v; void main(){ if(circ>.5 && dot(v,v)>1.) discard; gl_FragColor=vec4(k,1.); }");
	glBindAttribLocation(pq, 0, "p"); glLinkProgram(pq);
	uc = glGetUniformLocation(pq, "c"); uh = glGetUniformLocation(pq, "h"); ua = glGetUniformLocation(pq, "a"); ur = glGetUniformLocation(pq, "r");
	uk = glGetUniformLocation(pq, "k"); ucirc = glGetUniformLocation(pq, "circ");
	pp = prog("attribute vec2 pos; attribute vec3 col; uniform vec2 r; varying vec3 vc; void main(){ vc=col; gl_PointSize=16.; gl_Position=vec4(pos.x/r.x*2.-1.,1.-pos.y/r.y*2.,0.,1.); }",
		"precision mediump float; varying vec3 vc; void main(){ vec2 d=gl_PointCoord-.5; if(dot(d,d)>.25) discard; gl_FragColor=vec4(vc,1.); }");
	glBindAttribLocation(pp, 0, "pos"); glBindAttribLocation(pp, 1, "col"); glLinkProgram(pp);
	GLint ppr = glGetUniformLocation(pp, "r");

	/* fase 1: 9 objetivos */
	float TX[9], TY[9]; for (int i = 0; i < 9; i++) { TX[i] = W * (0.10f + 0.40f * (i % 3)); TY[i] = H * (0.12f + 0.38f * (i / 3)); }
	int tgt = 0, miss = 0; float sdx = 0, sdy = 0, serr = 0, maxerr = 0; float mdx[9] = {0}, mdy[9] = {0};
	/* fase 2: zigzag */
	float fx[NPATH] = { .12f, .88f, .88f, .12f, .12f, .88f }, fy[NPATH] = { .15f, .15f, .50f, .50f, .85f, .85f };
	float tot = 0; for (int i = 0; i < NPATH; i++) { PX[i] = fx[i] * W; PY[i] = fy[i] * H; PL[i] = tot; if (i + 1 < NPATH) tot += hypotf(fx[i + 1] * W - PX[i], fy[i + 1] * H - PY[i]); }
	float s_cur = 0, devmax = 0, devsum = 0; long devn = 0, mv2 = 0; Uint32 t2s = 0, t2e = 0; int started2 = 0;
	/* fase 3: dibujo libre */
	static GLfloat pts[3000 * 5]; int np = 0, maxc = 0; Uint32 t3s = 0;
	int stage = 1, quit = 0, sel = 0, sta = 0, skip = 0, fdown = 0, fup = 0; long nmv = 0; Uint32 t0 = SDL_GetTicks(), tst = t0;
	int done1 = 0, done2 = 0, done3 = 0;

	while (!quit && SDL_GetTicks() - t0 < (Uint32)total * 1000) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) quit = 1;
			else if (e.type == SDL_JOYBUTTONDOWN) { if (e.jbutton.button == 10) sel = 1; else if (e.jbutton.button == 11) sta = 1; else if (e.jbutton.button == 0) skip = 1; }
			else if (e.type == SDL_JOYBUTTONUP) { if (e.jbutton.button == 10) sel = 0; if (e.jbutton.button == 11) sta = 0; }
			else if (e.type == SDL_FINGERDOWN) fdown++; else if (e.type == SDL_FINGERUP) fup++;
		}
		if (sel && sta) quit = 1;
		touch_poll();
		int ncur = 0; for (int s = 0; s < NS; s++) ncur += sl[s].act;
		if (ncur > maxc) maxc = ncur;
		Uint32 now = SDL_GetTicks();
		for (int k = 0; k < nev; k++) {
			Ev *v = &evq[k]; if (v->type == 1) nmv++;
			if (stage == 1 && v->type == 0 && tgt < 9) {
				float dx = v->x - TX[tgt], dy = v->y - TY[tgt], d = hypotf(dx, dy);
				if (d < 90) { sdx += dx; sdy += dy; serr += d; mdx[tgt] = dx; mdy[tgt] = dy; if (d > maxerr) maxerr = d; LOG("  objetivo %d: error %.0f px (dx %+.0f, dy %+.0f)\n", tgt + 1, d, dx, dy); tgt++; }
				else { miss++; LOG("  toque fuera del objetivo %d: (%.0f,%.0f) vs (%.0f,%.0f) = %.0f px\n", tgt + 1, v->x, v->y, TX[tgt], TY[tgt], d); }
			}
			if (stage == 2 && v->type != 2 && !done2) {
				float best = -1, dmin = 1e9;
				for (int i = 0; i + 1 < NPATH; i++) {
					float t, d = dist_seg(v->x, v->y, i, &t), s = PL[i] + t * hypotf(PX[i + 1] - PX[i], PY[i + 1] - PY[i]);
					if (d < dmin) dmin = d;
					if (d < 80 && s > s_cur - 30 && s < s_cur + 160 && s > best) best = s;
				}
				if (best > s_cur) { if (!started2) { started2 = 1; t2s = now; } s_cur = best; }
				if (started2 && dmin < 150) { devsum += dmin; devn++; if (dmin > devmax) devmax = dmin; if (v->type == 1) mv2++; }
				if (s_cur >= tot - 25) { done2 = 1; t2e = now; }
			}
			if (stage == 3 && v->type != 2 && np < 3000) { GLfloat *q = pts + np * 5; q[0] = v->x; q[1] = v->y; q[2] = SC[v->slot][0]; q[3] = SC[v->slot][1]; q[4] = SC[v->slot][2]; np++; }
		}
		if (stage == 1 && tgt >= 9) { done1 = 1; skip = 1; }
		if (stage == 2 && done2) skip = 1;
		if (stage == 3 && (maxc >= 4 || now - t3s > 25000) && t3s) { done3 = (maxc >= 2); skip = 1; }
		if (skip && now - tst > 400) {
			skip = 0; stage++; tst = now; if (stage == 3) t3s = now; if (stage > 3) break;
			LOG("-- fase %d --\n", stage);
		} else skip = 0;
		if (stage == 3 && !t3s) t3s = now;

		glViewport(0, 0, W, H);
		float bg = stage == 3 ? 0.03f : 0.05f; glClearColor(bg, bg, 0.12f, 1); glClear(GL_COLOR_BUFFER_BIT);
		float pul = 0.5f + 0.5f * sinf(now * 0.008f);
		if (stage == 1) {
			for (int i = 0; i < 9; i++) {
				if (i < tgt) rect(TX[i], TY[i], 18, 18, 0, 1, .1f, .6f, .2f);
				else if (i == tgt) { rect(TX[i], TY[i], 60 + 8 * pul, 60 + 8 * pul, 0, 1, 1, .8f, .1f); rect(TX[i], TY[i], 44, 44, 0, 1, .05f, .05f, .12f); rect(TX[i], TY[i], 10, 10, 0, 1, 1, .8f, .1f); }
				else rect(TX[i], TY[i], 14, 14, 0, 1, .25f, .25f, .35f);
			}
		} else if (stage == 2) {
			for (int i = 0; i + 1 < NPATH; i++) seg(PX[i], PY[i], PX[i + 1], PY[i + 1], 64, .22f, .22f, .32f);
			for (int i = 0; i < NPATH; i++) rect(PX[i], PY[i], 32, 32, 0, 1, .22f, .22f, .32f);
			float acc = 0;
			for (int i = 0; i + 1 < NPATH; i++) {
				float l = hypotf(PX[i + 1] - PX[i], PY[i + 1] - PY[i]), u = (s_cur - acc) / l; if (u > 1) u = 1;
				if (u > 0) seg(PX[i], PY[i], PX[i] + (PX[i + 1] - PX[i]) * u, PY[i] + (PY[i + 1] - PY[i]) * u, 40, .15f, .8f, .3f);
				acc += l;
			}
			rect(PX[0], PY[0], 22, 22, 0, 1, 1, .8f, .1f);
			if (done2) rect(PX[NPATH - 1], PY[NPATH - 1], 30, 30, 0, 1, .2f, 1, .4f);
		} else {
			if (np) { glUseProgram(pp); glUniform2f(ppr, W, H); glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
				glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), pts); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), pts + 2); glDrawArrays(GL_POINTS, 0, np); }
			for (int i = 0; i < NS; i++) rect(40 + i * 44, 40, 16, 16, 0, 1, i < ncur ? SC[i][0] : .15f, i < ncur ? SC[i][1] : .15f, i < ncur ? SC[i][2] : .2f);
			for (int i = 0; i < maxc; i++) rect(40 + i * 44, 76, 6, 6, 0, 1, .2f, 1, .4f);
		}
		for (int s = 0; s < NS; s++) if (sl[s].act) {
			float px = sl[s].x * (W - 1) / axmax, py = sl[s].y * (H - 1) / aymax;
			rect(px, py, 34, 34, 0, 1, SC[s][0], SC[s][1], SC[s][2]); rect(px, py, 24, 24, 0, 1, bg, bg, .12f);
		}
		SDL_GL_SwapWindow(w);
	}
	LOG("=== RESULTADO ===\n");
	if (tgt) LOG("fase 1: %d/9 objetivos, %d toques fuera; error medio %.1f px, max %.1f; descentrado medio dx %+.1f dy %+.1f px\n", tgt, miss, serr / tgt, maxerr, sdx / tgt, sdy / tgt);
	else LOG("fase 1: 0/9 objetivos (%d toques fuera)\n", miss);
	LOG("fase 2: %s (recorrido %.0f%% de %.0f px); desvio medio %.1f px, max %.1f px; %ld muestras de movimiento%s\n", done2 ? "COMPLETA" : "incompleta", 100 * s_cur / tot, tot, devn ? devsum / devn : 0, devmax, mv2,
		done2 && t2e > t2s ? "" : "");
	if (done2 && t2e > t2s) LOG("        %.1f s, %.0f muestras/s\n", (t2e - t2s) / 1000.0f, mv2 * 1000.0f / (t2e - t2s));
	LOG("fase 3: contactos simultaneos maximos %d (el panel anuncia %d); %d puntos dibujados\n", maxc, NS, np);
	if (tgt) { LOG("error por objetivo (fila 1 arriba, 3 abajo; columna 1 izq, 3 der):\n"); for (int i = 0; i < tgt; i++) LOG("  (%d,%d) dx %+.0f dy %+.0f\n", i % 3 + 1, i / 3 + 1, mdx[i], mdy[i]); }
	LOG("eventos MT de movimiento: %ld; SDL: %d FINGERDOWN, %d FINGERUP\n", nmv, fdown, fup);
	LOG("fases superadas: %d%d%d\n", done1, done2, done3);
	SDL_GL_DeleteContext(c); SDL_DestroyWindow(w); SDL_Quit(); if (lg) fclose(lg);
	return 0;
}
