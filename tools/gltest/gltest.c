#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#define W 256
#define H 256
#define BLOB "/vendor/lib64/egl/libGLES_mali.so"

static void *lib;
static int step;

#define FN(ret, name, args) static ret (*p_##name) args
FN(EGLDisplay, eglGetDisplay, (EGLNativeDisplayType));
FN(EGLBoolean, eglInitialize, (EGLDisplay, EGLint *, EGLint *));
FN(const char *, eglQueryString, (EGLDisplay, EGLint));
FN(EGLBoolean, eglBindAPI, (EGLenum));
FN(EGLBoolean, eglChooseConfig, (EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *));
FN(EGLSurface, eglCreatePbufferSurface, (EGLDisplay, EGLConfig, const EGLint *));
FN(EGLContext, eglCreateContext, (EGLDisplay, EGLConfig, EGLContext, const EGLint *));
FN(EGLBoolean, eglMakeCurrent, (EGLDisplay, EGLSurface, EGLSurface, EGLContext));
FN(EGLint, eglGetError, (void));
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
FN(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *));
FN(void, glUseProgram, (GLuint));
FN(void, glBindAttribLocation, (GLuint, GLuint, const GLchar *));
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

static void ok(void) { printf("OK paso %d\n", step); fflush(stdout); }
static void die(const char *what)
{
	printf("FALLO paso %d: %s eglGetError=0x%x glGetError=0x%x\n", step, what,
	       p_eglGetError ? p_eglGetError() : 0, p_glGetError ? p_glGetError() : 0);
	fflush(stdout);
	exit(2);
}

static void *sym(const char *n)
{
	void *p = dlsym(lib, n);
	if (!p) { printf("FALLO paso %d: dlsym(%s) no existe\n", step, n); fflush(stdout); exit(2); }
	return p;
}
#define L(n) p_##n = sym(#n)

static const char *vs_src =
	"#version 300 es\n"
	"in vec2 pos; in vec3 col; out vec3 vcol;\n"
	"void main(){ vcol=col; gl_Position=vec4(pos,0.0,1.0); }\n";
static const char *fs_src =
	"#version 300 es\n"
	"precision mediump float; in vec3 vcol; out vec4 o;\n"
	"void main(){ o=vec4(vcol,1.0); }\n";

static GLuint shader(GLenum type, const char *src)
{
	GLint s = 0;
	char log[512] = "";
	GLuint sh = p_glCreateShader(type);
	p_glShaderSource(sh, 1, &src, NULL);
	p_glCompileShader(sh);
	p_glGetShaderiv(sh, GL_COMPILE_STATUS, &s);
	if (!s) { p_glGetShaderInfoLog(sh, sizeof log, NULL, log); printf("shader: %s\n", log); die("compilar shader"); }
	return sh;
}

int main(void)
{
	EGLint maj = 0, min = 0, n = 0;
	const char *ext;
	EGLDisplay dpy;
	EGLConfig cfg;
	EGLSurface surf = EGL_NO_SURFACE;
	EGLContext ctx;
	int surfaceless = 0;

	step = 1;
	lib = dlopen(BLOB, RTLD_NOW);
	if (!lib) { printf("FALLO paso 1: dlopen %s: %s\n", BLOB, dlerror()); fflush(stdout); return 2; }
	L(eglGetDisplay); L(eglInitialize); L(eglQueryString); L(eglBindAPI); L(eglChooseConfig);
	L(eglCreatePbufferSurface); L(eglCreateContext); L(eglMakeCurrent); L(eglGetError);
	L(glGetString); L(glClearColor); L(glClear); L(glViewport); L(glCreateShader); L(glShaderSource);
	L(glCompileShader); L(glGetShaderiv); L(glGetShaderInfoLog); L(glCreateProgram); L(glAttachShader);
	L(glLinkProgram); L(glGetProgramiv); L(glGetProgramInfoLog); L(glUseProgram); L(glBindAttribLocation);
	L(glEnableVertexAttribArray); L(glVertexAttribPointer); L(glDrawArrays); L(glFinish);
	L(glReadPixels); L(glGetError); L(glGenFramebuffers); L(glBindFramebuffer); L(glGenRenderbuffers);
	L(glBindRenderbuffer); L(glRenderbufferStorage); L(glFramebufferRenderbuffer); L(glCheckFramebufferStatus);
	ok();

	step = 2;
	dpy = p_eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (dpy == EGL_NO_DISPLAY) die("eglGetDisplay");
	ok();

	step = 3;
	if (!p_eglInitialize(dpy, &maj, &min)) die("eglInitialize");
	printf("EGL %d.%d\n", maj, min);
	ok();

	step = 4;
	printf("EGL_VENDOR: %s\nEGL_VERSION: %s\n", p_eglQueryString(dpy, EGL_VENDOR), p_eglQueryString(dpy, EGL_VERSION));
	ext = p_eglQueryString(dpy, EGL_EXTENSIONS);
	printf("EGL_EXTENSIONS: %s\n", ext ? ext : "(null)");
	ok();

	step = 5;
	if (!p_eglBindAPI(EGL_OPENGL_ES_API)) die("eglBindAPI");
	ok();

	step = 6;
	{
		EGLint attr[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
				  EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
		if (!p_eglChooseConfig(dpy, attr, &cfg, 1, &n) || n < 1) {
			printf("sin config pbuffer GLES3 (n=%d); pruebo sin PBUFFER\n", n);
			EGLint a2[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE };
			n = 0;
			if (!p_eglChooseConfig(dpy, a2, &cfg, 1, &n) || n < 1) die("eglChooseConfig");
			surfaceless = 1;
		}
	}
	ok();

	step = 7;
	if (!surfaceless) {
		EGLint pa[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
		surf = p_eglCreatePbufferSurface(dpy, cfg, pa);
		if (surf == EGL_NO_SURFACE) {
			printf("pbuffer fallo (eglGetError=0x%x); pruebo surfaceless\n", p_eglGetError());
			surfaceless = 1;
		}
	}
	if (surfaceless && !(ext && strstr(ext, "EGL_KHR_surfaceless_context"))) die("pbuffer y sin surfaceless_context");
	ok();

	step = 8;
	{
		EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
		ctx = p_eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ca);
		if (ctx == EGL_NO_CONTEXT) die("eglCreateContext");
		if (!p_eglMakeCurrent(dpy, surf, surf, ctx)) die("eglMakeCurrent");
	}
	printf("GL_VENDOR: %s\nGL_RENDERER: %s\nGL_VERSION: %s\n",
	       p_glGetString(GL_VENDOR), p_glGetString(GL_RENDERER), p_glGetString(GL_VERSION));
	ok();

	if (surfaceless) {
		GLuint fbo, rb;
		step = 9;
		p_glGenFramebuffers(1, &fbo);
		p_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		p_glGenRenderbuffers(1, &rb);
		p_glBindRenderbuffer(GL_RENDERBUFFER, rb);
		p_glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, H);
		p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
		if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) die("FBO incompleto");
		ok();
	}

	step = 10;
	{
		GLuint vs = shader(GL_VERTEX_SHADER, vs_src), fs = shader(GL_FRAGMENT_SHADER, fs_src), pr;
		GLint s = 0;
		char log[512] = "";
		static const GLfloat v[] = {
			-0.8f, -0.8f, 1, 0, 0,
			 0.8f, -0.8f, 0, 1, 0,
			 0.0f,  0.8f, 1, 1, 0,
		};
		pr = p_glCreateProgram();
		p_glAttachShader(pr, vs);
		p_glAttachShader(pr, fs);
		p_glBindAttribLocation(pr, 0, "pos");
		p_glBindAttribLocation(pr, 1, "col");
		p_glLinkProgram(pr);
		p_glGetProgramiv(pr, GL_LINK_STATUS, &s);
		if (!s) { p_glGetProgramInfoLog(pr, sizeof log, NULL, log); printf("link: %s\n", log); die("link"); }
		p_glUseProgram(pr);
		p_glViewport(0, 0, W, H);
		p_glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
		p_glClear(GL_COLOR_BUFFER_BIT);
		p_glEnableVertexAttribArray(0);
		p_glEnableVertexAttribArray(1);
		p_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), v);
		p_glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), v + 2);
		p_glDrawArrays(GL_TRIANGLES, 0, 3);
		p_glFinish();
		if (p_glGetError() != GL_NO_ERROR) die("error GL tras dibujar");
	}
	ok();

	step = 11;
	{
		static unsigned char buf[W * H * 4];
		uint32_t sum = 0;
		const unsigned char *c, *k;
		FILE *f;
		p_glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, buf);
		if (p_glGetError() != GL_NO_ERROR) die("glReadPixels");
		for (int i = 0; i < W * H * 4; i++) sum = sum * 31 + buf[i];
		c = buf + ((H / 2) * W + W / 2) * 4;
		k = buf;
		printf("pixel centro: %u %u %u %u\n", c[0], c[1], c[2], c[3]);
		printf("pixel esquina(0,0): %u %u %u %u\n", k[0], k[1], k[2], k[3]);
		printf("checksum: 0x%08x\n", sum);
		f = fopen("/tmp/gltest.ppm", "wb");
		if (f) {
			fprintf(f, "P6\n%d %d\n255\n", W, H);
			for (int y = H - 1; y >= 0; y--)
				for (int x = 0; x < W; x++) fwrite(buf + (y * W + x) * 4, 1, 3, f);
			fclose(f);
		}
		ok();
		fflush(stdout);
		if (c[0] == 0 && c[1] == 0 && c[2] == 255) { printf("FALLO: el centro es el color de fondo\n"); fflush(stdout); return 2; }
	}
	printf("RESULTADO: render correcto\n");
	fflush(stdout);
	return 0;
}
