/* Programa glibc normal que renderiza con la Mali via libhybris (plataforma EGL "drm"). Uso: hybristest [segundos] */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static const char *VS = "attribute vec2 p; uniform float a; varying vec2 uv;"
	"void main(){ float c=cos(a), s=sin(a); uv=p; gl_Position=vec4(vec2(c*p.x-s*p.y, s*p.x+c*p.y)*0.7,0.0,1.0); }";
static const char *FS = "precision mediump float; varying vec2 uv; uniform float a;"
	"void main(){ gl_FragColor=vec4(0.5+0.5*sin(uv.x*6.0+a*3.0), 0.5+0.5*sin(uv.y*6.0-a*2.0), 0.5+0.5*cos(a), 1.0); }";

static GLuint sh(GLenum t, const char *s)
{
	GLuint o = glCreateShader(t); GLint ok; char log[512];
	glShaderSource(o, 1, &s, 0); glCompileShader(o); glGetShaderiv(o, GL_COMPILE_STATUS, &ok);
	if (!ok) { glGetShaderInfoLog(o, sizeof log, 0, log); printf("shader: %s\n", log); exit(2); }
	return o;
}

int main(int argc, char **argv)
{
	int secs = argc > 1 ? atoi(argv[1]) : 8, n = 0, frames = 0;
	EGLint maj, min, nc = 0;
	EGLConfig cfg;
	EGLint ca[] = { EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
	EGLint cx[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	double t0, tn;
	(void)n;
	if (!eglInitialize(d, &maj, &min)) { printf("FALLO eglInitialize 0x%x\n", eglGetError()); return 2; }
	printf("EGL %d.%d vendor=%s\n", maj, min, eglQueryString(d, EGL_VENDOR)); fflush(stdout);
	eglBindAPI(EGL_OPENGL_ES_API);
	if (!eglChooseConfig(d, ca, &cfg, 1, &nc) || nc < 1) { printf("FALLO eglChooseConfig 0x%x\n", eglGetError()); return 2; }
	EGLSurface s = eglCreateWindowSurface(d, cfg, (EGLNativeWindowType)0, NULL);
	if (s == EGL_NO_SURFACE) { printf("FALLO eglCreateWindowSurface 0x%x\n", eglGetError()); return 3; }
	EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, cx);
	if (c == EGL_NO_CONTEXT || !eglMakeCurrent(d, s, s, c)) { printf("FALLO contexto 0x%x\n", eglGetError()); return 3; }
	EGLint w = 0, h = 0; eglQuerySurface(d, s, EGL_WIDTH, &w); eglQuerySurface(d, s, EGL_HEIGHT, &h);
	printf("GL_RENDERER=%s GL_VERSION=%s superficie %dx%d\n", glGetString(GL_RENDERER), glGetString(GL_VERSION), w, h); fflush(stdout);
	GLuint pr = glCreateProgram(); glAttachShader(pr, sh(GL_VERTEX_SHADER, VS)); glAttachShader(pr, sh(GL_FRAGMENT_SHADER, FS));
	glBindAttribLocation(pr, 0, "p"); glLinkProgram(pr); glUseProgram(pr);
	GLint ua = glGetUniformLocation(pr, "a");
	static const GLfloat v[] = { -1,-1, 1,-1, -1,1, 1,1 };
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, v); glEnableVertexAttribArray(0);
	t0 = now();
	while ((tn = now()) - t0 < secs) {
		glViewport(0, 0, w, h);
		glClearColor(0.05f, 0.05f, 0.2f, 1); glClear(GL_COLOR_BUFFER_BIT);
		glUniform1f(ua, (float)(tn - t0));
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		if (!eglSwapBuffers(d, s)) { printf("FALLO eglSwapBuffers 0x%x\n", eglGetError()); return 4; }
		frames++;
	}
	printf("RESULTADO: %d frames en %.2f s = %.1f fps; glGetError=0x%x\nFIN\n", frames, tn - t0, frames / (tn - t0), glGetError());
	return 0;
}
