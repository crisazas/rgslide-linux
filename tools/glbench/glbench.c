/* Benchmark GLES3 offscreen: mismo codigo para el blob de Arm (bionic) y para Mesa (glibc).
 * uso: glbench EGL_LIB GLES_LIB [ancho alto segundos]
 *   Mali : glbench /vendor/lib64/egl/libGLES_mali.so /vendor/lib64/egl/libGLES_mali.so
 *   Mesa : EGL_PLATFORM=surfaceless glbench libEGL.so.1 libGLESv2.so.2
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

static void *elib, *glib;

#define FN(ret, name, args) static ret (*p_##name) args
FN(EGLDisplay, eglGetDisplay, (EGLNativeDisplayType));
FN(EGLBoolean, eglInitialize, (EGLDisplay, EGLint *, EGLint *));
FN(EGLBoolean, eglBindAPI, (EGLenum));
FN(EGLBoolean, eglChooseConfig, (EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *));
FN(EGLSurface, eglCreatePbufferSurface, (EGLDisplay, EGLConfig, const EGLint *));
FN(EGLContext, eglCreateContext, (EGLDisplay, EGLConfig, EGLContext, const EGLint *));
FN(EGLBoolean, eglMakeCurrent, (EGLDisplay, EGLSurface, EGLSurface, EGLContext));
FN(EGLint, eglGetError, (void));
FN(void *, eglGetProcAddress, (const char *));
FN(const GLubyte *, glGetString, (GLenum));
FN(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat));
FN(void, glClear, (GLbitfield));
FN(void, glViewport, (GLint, GLint, GLsizei, GLsizei));
FN(GLuint, glCreateShader, (GLenum));
FN(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *));
FN(void, glCompileShader, (GLuint));
FN(void, glGetShaderiv, (GLuint, GLenum, GLint *));
FN(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *));
FN(GLuint, glCreateProgram, (void));
FN(void, glAttachShader, (GLuint, GLuint));
FN(void, glLinkProgram, (GLuint));
FN(void, glGetProgramiv, (GLuint, GLenum, GLint *));
FN(void, glUseProgram, (GLuint));
FN(GLint, glGetUniformLocation, (GLuint, const GLchar *));
FN(void, glUniform1f, (GLint, GLfloat));
FN(void, glEnableVertexAttribArray, (GLuint));
FN(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *));
FN(void, glDrawArrays, (GLenum, GLint, GLsizei));
FN(void, glFinish, (void));
FN(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *));
FN(GLenum, glGetError, (void));
FN(void, glGenFramebuffers, (GLsizei, GLuint *));
FN(void, glBindFramebuffer, (GLenum, GLuint));
FN(void, glGenRenderbuffers, (GLsizei, GLuint *));
FN(void, glBindRenderbuffer, (GLenum, GLuint));
FN(void, glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei));
FN(void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint));
FN(GLenum, glCheckFramebufferStatus, (GLenum));
FN(void, glGenBuffers, (GLsizei, GLuint *));
FN(void, glBindBuffer, (GLenum, GLuint));
FN(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum));

static void *sym(const char *n)
{
	void *p = glib ? dlsym(glib, n) : 0;
	if (!p && elib) p = dlsym(elib, n);
	if (!p && p_eglGetProcAddress) p = p_eglGetProcAddress(n);
	if (!p) { printf("FALLO: simbolo %s no existe\n", n); fflush(stdout); exit(2); }
	return p;
}
#define L(n) p_##n = sym(#n)

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec * 1e-9;
}

static void die(const char *w)
{
	printf("FALLO: %s eglGetError=0x%x glGetError=0x%x\n", w, p_eglGetError ? p_eglGetError() : 0, p_glGetError ? p_glGetError() : 0);
	fflush(stdout);
	exit(2);
}

static GLuint prog(const char *vs, const char *fs)
{
	GLuint v = p_glCreateShader(GL_VERTEX_SHADER), f = p_glCreateShader(GL_FRAGMENT_SHADER), p;
	const char *s[1];
	GLint ok;
	char log[1024];
	s[0] = vs; p_glShaderSource(v, 1, s, 0); p_glCompileShader(v);
	p_glGetShaderiv(v, GL_COMPILE_STATUS, &ok);
	if (!ok) { p_glGetShaderInfoLog(v, sizeof log, 0, log); printf("vs: %s\n", log); die("compilar vs"); }
	s[0] = fs; p_glShaderSource(f, 1, s, 0); p_glCompileShader(f);
	p_glGetShaderiv(f, GL_COMPILE_STATUS, &ok);
	if (!ok) { p_glGetShaderInfoLog(f, sizeof log, 0, log); printf("fs: %s\n", log); die("compilar fs"); }
	p = p_glCreateProgram(); p_glAttachShader(p, v); p_glAttachShader(p, f); p_glLinkProgram(p);
	p_glGetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok) die("enlazar");
	return p;
}

static const char *VS_QUAD =
	"#version 300 es\nlayout(location=0) in vec2 a; out vec2 uv;\n"
	"void main(){ uv=a*0.5+0.5; gl_Position=vec4(a,0.0,1.0); }\n";
static const char *FS_ALU =
	"#version 300 es\nprecision highp float; uniform float t; in vec2 uv; out vec4 o;\n"
	"void main(){ vec2 p=uv*3.0; float a=0.0;\n"
	" for(int i=0;i<48;i++){ p=vec2(sin(p.y+t)+p.x*0.7, cos(p.x-t)+p.y*0.7); a+=length(p); }\n"
	" o=vec4(fract(a*0.01),fract(a*0.013),fract(a*0.017),1.0); }\n";
static const char *VS_TRI =
	"#version 300 es\nlayout(location=0) in vec2 a; uniform float t; out vec2 uv;\n"
	"void main(){ uv=a*0.5+0.5; gl_Position=vec4(a+vec2(sin(t),cos(t))*0.01,0.0,1.0); }\n";
static const char *FS_SIMPLE =
	"#version 300 es\nprecision mediump float; in vec2 uv; out vec4 o;\n"
	"void main(){ o=vec4(uv,0.5,1.0); }\n";

static uint32_t rng = 12345;
static float rnd(void) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; }

int main(int argc, char **argv)
{
	EGLDisplay dpy;
	EGLConfig cfg;
	EGLSurface surf;
	EGLContext ctx;
	EGLint maj, min, n = 0;
	int W = 1280, H = 960;
	double secs = 4.0;
	GLuint fbo, rb, pa, pt, vbo, vbo2;
	GLint ut_a, ut_t;
	unsigned char px[4];
	double t0, dt;
	int frames;
	uint32_t sum = 0;

	if (argc < 3) { printf("uso: %s EGL_LIB GLES_LIB [ancho alto segundos]\n", argv[0]); return 1; }
	if (argc > 3) W = atoi(argv[3]);
	if (argc > 4) H = atoi(argv[4]);
	if (argc > 5) secs = atof(argv[5]);

	elib = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
	if (!elib) { printf("FALLO dlopen %s: %s\n", argv[1], dlerror()); return 2; }
	glib = strcmp(argv[1], argv[2]) ? dlopen(argv[2], RTLD_NOW | RTLD_GLOBAL) : elib;
	if (!glib) { printf("FALLO dlopen %s: %s\n", argv[2], dlerror()); return 2; }
	p_eglGetProcAddress = dlsym(elib, "eglGetProcAddress");
	L(eglGetDisplay); L(eglInitialize); L(eglBindAPI); L(eglChooseConfig); L(eglCreatePbufferSurface);
	L(eglCreateContext); L(eglMakeCurrent); L(eglGetError);
	L(glGetString); L(glClearColor); L(glClear); L(glViewport); L(glCreateShader); L(glShaderSource);
	L(glCompileShader); L(glGetShaderiv); L(glGetShaderInfoLog); L(glCreateProgram); L(glAttachShader);
	L(glLinkProgram); L(glGetProgramiv); L(glUseProgram); L(glGetUniformLocation); L(glUniform1f);
	L(glEnableVertexAttribArray); L(glVertexAttribPointer); L(glDrawArrays); L(glFinish); L(glReadPixels);
	L(glGetError); L(glGenFramebuffers); L(glBindFramebuffer); L(glGenRenderbuffers); L(glBindRenderbuffer);
	L(glRenderbufferStorage); L(glFramebufferRenderbuffer); L(glCheckFramebufferStatus);
	L(glGenBuffers); L(glBindBuffer); L(glBufferData);

	dpy = p_eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (dpy == EGL_NO_DISPLAY) die("eglGetDisplay");
	if (!p_eglInitialize(dpy, &maj, &min)) die("eglInitialize");
	if (!p_eglBindAPI(EGL_OPENGL_ES_API)) die("eglBindAPI");
	{
		EGLint a1[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
				EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
		EGLint a2[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE };
		if (!p_eglChooseConfig(dpy, a1, &cfg, 1, &n) || n < 1) {
			n = 0;
			if (!p_eglChooseConfig(dpy, a2, &cfg, 1, &n) || n < 1) die("eglChooseConfig");
		}
	}
	{
		EGLint pb[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
		EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
		surf = p_eglCreatePbufferSurface(dpy, cfg, pb);
		ctx = p_eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ca);
		if (ctx == EGL_NO_CONTEXT) die("eglCreateContext");
		if (!p_eglMakeCurrent(dpy, surf, surf, ctx)) die("eglMakeCurrent");
	}
	printf("GL_RENDERER: %s\nGL_VERSION: %s\nresolucion: %dx%d, %.1f s por prueba\n",
	       p_glGetString(GL_RENDERER), p_glGetString(GL_VERSION), W, H, secs);
	fflush(stdout);

	p_glGenFramebuffers(1, &fbo); p_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	p_glGenRenderbuffers(1, &rb); p_glBindRenderbuffer(GL_RENDERBUFFER, rb);
	p_glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, H);
	p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
	if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) die("FBO incompleto");
	p_glViewport(0, 0, W, H);

	{
		static const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, 1, -1, 1, 1, -1, 1 };
		p_glGenBuffers(1, &vbo); p_glBindBuffer(GL_ARRAY_BUFFER, vbo);
		p_glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
	}
	{
		int nt = 100000, i;
		GLfloat *v = malloc((size_t)nt * 6 * sizeof(GLfloat));
		for (i = 0; i < nt; i++) {
			float cx = rnd() * 2 - 1, cy = rnd() * 2 - 1, s = 0.004f;
			v[i * 6 + 0] = cx;     v[i * 6 + 1] = cy;
			v[i * 6 + 2] = cx + s; v[i * 6 + 3] = cy;
			v[i * 6 + 4] = cx;     v[i * 6 + 5] = cy + s;
		}
		p_glGenBuffers(1, &vbo2); p_glBindBuffer(GL_ARRAY_BUFFER, vbo2);
		p_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nt * 6 * sizeof(GLfloat), v, GL_STATIC_DRAW);
		free(v);
	}
	pa = prog(VS_QUAD, FS_ALU); ut_a = p_glGetUniformLocation(pa, "t");
	pt = prog(VS_TRI, FS_SIMPLE); ut_t = p_glGetUniformLocation(pt, "t");

	/* --- prueba A: relleno con shader pesado (3 pasadas de pantalla completa por frame) --- */
	p_glBindBuffer(GL_ARRAY_BUFFER, vbo);
	p_glEnableVertexAttribArray(0); p_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
	p_glUseProgram(pa);
	for (frames = 0, t0 = now(); now() - t0 < 1.0; frames++) { p_glUniform1f(ut_a, frames * 0.01f); p_glDrawArrays(GL_TRIANGLES, 0, 6); p_glFinish(); }
	for (frames = 0, t0 = now(); (dt = now() - t0) < secs; frames++) {
		int k;
		p_glUniform1f(ut_a, frames * 0.01f);
		for (k = 0; k < 3; k++) p_glDrawArrays(GL_TRIANGLES, 0, 6);
		p_glFinish();
	}
	printf("A shader-ALU : %d frames en %.2f s = %.2f fps, %.1f ms/frame, %.1f Mpix/s (3 pasadas)\n",
	       frames, dt, frames / dt, 1000.0 * dt / frames, 3.0 * W * H * frames / dt / 1e6);
	fflush(stdout);
	p_glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	sum += px[0] + px[1] * 3 + px[2] * 7;
	printf("  pixel centro tras A: %d %d %d %d\n", px[0], px[1], px[2], px[3]);

	/* --- prueba B: geometria, 100000 triangulos pequenos x 10 por frame --- */
	p_glBindBuffer(GL_ARRAY_BUFFER, vbo2);
	p_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
	p_glUseProgram(pt);
	for (frames = 0, t0 = now(); now() - t0 < 1.0; frames++) { p_glUniform1f(ut_t, frames * 0.01f); p_glDrawArrays(GL_TRIANGLES, 0, 300000); p_glFinish(); }
	for (frames = 0, t0 = now(); (dt = now() - t0) < secs; frames++) {
		int k;
		p_glUniform1f(ut_t, frames * 0.01f);
		for (k = 0; k < 10; k++) p_glDrawArrays(GL_TRIANGLES, 0, 300000);
		p_glFinish();
	}
	printf("B geometria  : %d frames en %.2f s = %.2f fps, %.1f ms/frame, %.1f Mtri/s (1M tri/frame)\n",
	       frames, dt, frames / dt, 1000.0 * dt / frames, 1.0 * frames * 1e6 / dt / 1e6);
	fflush(stdout);
	p_glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	sum += px[0] + px[1] * 3 + px[2] * 7;
	printf("  pixel centro tras B: %d %d %d %d\n", px[0], px[1], px[2], px[3]);
	printf("glGetError final = 0x%x, checksum %u\nFIN\n", p_glGetError(), sum);
	return 0;
}
