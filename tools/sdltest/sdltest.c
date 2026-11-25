/* sdltest: prueba SDL2 (driver hybris) -> ventana GLES2 a pantalla completa, dibuja con la Mali, muestra los eventos de entrada.
 * Uso: sdltest [segundos]. Los botones del mando se imprimen y cambian el color de fondo; Start+Select (botones 10 y 11 del mando) salen. */
#include <SDL.h>
#include <SDL_opengles2.h>
#include <stdio.h>
#include <stdlib.h>

static GLuint sh(GLenum t, const char *s) { GLuint o = glCreateShader(t); glShaderSource(o, 1, &s, 0); glCompileShader(o); return o; }

int main(int argc, char **argv)
{
	int secs = argc > 1 ? atoi(argv[1]) : 10;
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) < 0) { printf("FALLO SDL_Init: %s\n", SDL_GetError()); return 2; }
	printf("SDL %d.%d.%d video=%s\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL, SDL_GetCurrentVideoDriver());
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_Window *w = SDL_CreateWindow("sdltest", 0, 0, 1280, 960, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
	if (!w) { printf("FALLO SDL_CreateWindow: %s\n", SDL_GetError()); return 3; }
	SDL_GLContext c = SDL_GL_CreateContext(w);
	if (!c) { printf("FALLO SDL_GL_CreateContext: %s\n", SDL_GetError()); return 4; }
	int ww, hh; SDL_GetWindowSize(w, &ww, &hh);
	printf("ventana %dx%d GL_RENDERER=%s GL_VERSION=%s\n", ww, hh, glGetString(GL_RENDERER), glGetString(GL_VERSION));
	printf("joysticks: %d\n", SDL_NumJoysticks());
	for (int i = 0; i < SDL_NumJoysticks(); i++) { SDL_Joystick *j = SDL_JoystickOpen(i); printf("  [%d] %s botones=%d ejes=%d hats=%d\n", i, SDL_JoystickName(j), SDL_JoystickNumButtons(j), SDL_JoystickNumAxes(j), SDL_JoystickNumHats(j)); }
	GLuint pr = glCreateProgram();
	glAttachShader(pr, sh(GL_VERTEX_SHADER, "attribute vec2 p; uniform float a; void main(){ float c=cos(a),s=sin(a); gl_Position=vec4(vec2(c*p.x-s*p.y,s*p.x+c*p.y)*0.5,0.,1.); }"));
	glAttachShader(pr, sh(GL_FRAGMENT_SHADER, "precision mediump float; uniform vec3 k; void main(){ gl_FragColor=vec4(k,1.); }"));
	glBindAttribLocation(pr, 0, "p"); glLinkProgram(pr); glUseProgram(pr);
	GLint ua = glGetUniformLocation(pr, "a"), uk = glGetUniformLocation(pr, "k");
	static const GLfloat v[] = { -1,-1, 1,-1, -1,1, 1,1 };
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, v); glEnableVertexAttribArray(0);
	float bg = 0.1f; int frames = 0, nev = 0, quit = 0, sel = 0, sta = 0; Uint32 t0 = SDL_GetTicks();
	while (!quit && SDL_GetTicks() - t0 < (Uint32)secs * 1000) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			nev++;
			switch (e.type) {
			case SDL_QUIT: quit = 1; break;
			case SDL_KEYDOWN: printf("tecla %s\n", SDL_GetKeyName(e.key.keysym.sym)); fflush(stdout); break;
			case SDL_JOYBUTTONDOWN: printf("boton %d\n", e.jbutton.button); fflush(stdout); bg = 0.1f + 0.05f * (e.jbutton.button % 16); if (e.jbutton.button == 10) sel = 1; if (e.jbutton.button == 11) sta = 1; break;
			case SDL_JOYBUTTONUP: if (e.jbutton.button == 10) sel = 0; if (e.jbutton.button == 11) sta = 0; break;
			case SDL_JOYHATMOTION: printf("hat %d\n", e.jhat.value); fflush(stdout); break;
			case SDL_JOYAXISMOTION: if (abs(e.jaxis.value) > 12000) { printf("eje %d = %d\n", e.jaxis.axis, e.jaxis.value); fflush(stdout); } break;
			case SDL_FINGERDOWN: printf("toque %.2f,%.2f\n", e.tfinger.x, e.tfinger.y); fflush(stdout); break;
			}
			if (sel && sta) quit = 1;
		}
		float t = (SDL_GetTicks() - t0) * 0.001f;
		glViewport(0, 0, ww, hh); glClearColor(bg, 0.1f, 0.25f, 1); glClear(GL_COLOR_BUFFER_BIT);
		glUniform1f(ua, t); glUniform3f(uk, 0.5f + 0.5f * sinf(t * 2), 0.8f, 0.3f); glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		SDL_GL_SwapWindow(w); frames++;
	}
	float el = (SDL_GetTicks() - t0) * 0.001f;
	printf("RESULTADO: %d frames en %.2f s = %.1f fps; %d eventos\n", frames, el, frames / el, nev);
	SDL_GL_DeleteContext(c); SDL_DestroyWindow(w); SDL_Quit();
	printf("FIN\n");
	return 0;
}
