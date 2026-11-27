/* Idioma de rgslide-ui (es|en), sin dependencias. Los textos del programa y de los .menu son CLAVES en espanol: tr(clave) devuelve la
 * traduccion de $RGSLIDE_LANGDIR/<idioma>.tr (lineas clave=texto, '#' comentario) o la propia clave si no hay traduccion.
 * Idioma: RGSLIDE_LANG > /etc/default/rgslide-lang > LC_ALL/LC_MESSAGES/LANG > /etc/default/locale > /etc/locale.conf; sin nada: es; C/otro: en.
 * Misma regla que rgslide-ctl (lang_show) y que el parche de EmulationStation (patches/es/0001): ver docs/agent-lang.md. */
#ifndef RGSLIDE_LANG_H
#define RGSLIDE_LANG_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LANG_DIR_DEFAULT "/usr/share/rgslide/lang"

static char lang_code[8] = "es";
static char **lang_k, **lang_v;
static int lang_n;

static void lang_trim(char *s)
{
	char *w = s;
	for (char *r = s; *r; r++) if (*r != '"' && *r != '\'' && *r != ' ' && *r != '\t' && *r != '\r' && *r != '\n') *w++ = *r;
	*w = 0;
}

/* valor de CLAVE=valor en un fichero tipo shell (ultima aparicion), "" si no esta */
static void lang_file_var(const char *path, const char *key, char *out, size_t cap)
{
	FILE *fp = fopen(path, "r");
	char line[256];
	size_t kl = strlen(key);
	out[0] = 0;
	if (!fp) return;
	while (fgets(line, sizeof line, fp))
		if (!strncmp(line, key, kl) && line[kl] == '=') { snprintf(out, cap, "%s", line + kl + 1); lang_trim(out); }
	fclose(fp);
}

static void lang_esde(char *out, size_t cap)
{
	const char *h = getenv("HOME");
	char path[256], line[512];
	out[0] = 0;
	snprintf(path, sizeof path, "%s/ES-DE/settings/es_settings.xml", (h && *h) ? h : "/home/console");
	FILE *fp = fopen(path, "r");
	if (!fp && h && *h) fp = fopen("/home/console/ES-DE/settings/es_settings.xml", "r");
	if (!fp) return;
	while (fgets(line, sizeof line, fp)) {
		char *p = strstr(line, "name=\"ApplicationLanguage\"");
		char *q = p ? strstr(p, "value=\"") : NULL;
		if (q) {
			q += 7;
			char *end = strchr(q, '"');
			if (end) {
				size_t len = (size_t)(end - q);
				if (len >= cap) len = cap - 1;
				memcpy(out, q, len); out[len] = 0;
				if (!strcmp(out, "automatic")) out[0] = 0;
			}
			break;
		}
	}
	fclose(fp);
}

static const char *lang_detect(void)
{
	char v[96];
	const char *e = getenv("RGSLIDE_LANG");
	if (e && (!strcmp(e, "es") || !strcmp(e, "en"))) return e;
	lang_file_var("/etc/default/rgslide-lang", "RGSLIDE_LANG", v, sizeof v);
	if (!strcmp(v, "es")) return "es";
	if (!strcmp(v, "en")) return "en";
	lang_esde(v, sizeof v);
	const char *envs[3] = {getenv("LC_ALL"), getenv("LC_MESSAGES"), getenv("LANG")};
	for (int i = 0; i < 3 && !v[0]; i++) if (envs[i] && *envs[i]) snprintf(v, sizeof v, "%s", envs[i]);
	if (!v[0]) lang_file_var("/etc/default/locale", "LANG", v, sizeof v);
	if (!v[0]) lang_file_var("/etc/locale.conf", "LANG", v, sizeof v);
	if (!v[0]) return "es";
	return (v[0] == 'e' && v[1] == 's' && (!v[2] || v[2] == '_' || v[2] == '.' || v[2] == '@')) ? "es" : "en";
}

static void lang_init(void)
{
	const char *dir = getenv("RGSLIDE_LANGDIR");
	char path[256], line[1024];
	snprintf(lang_code, sizeof lang_code, "%s", lang_detect());
	snprintf(path, sizeof path, "%s/%s.tr", dir && *dir ? dir : LANG_DIR_DEFAULT, lang_code);
	FILE *fp = fopen(path, "r");
	if (!fp) return;
	while (fgets(line, sizeof line, fp)) {
		line[strcspn(line, "\r\n")] = 0;
		char *eq = strchr(line, '=');
		if (!*line || *line == '#' || !eq || eq == line) continue;
		*eq = 0;
		lang_k = realloc(lang_k, (lang_n + 1) * sizeof *lang_k); lang_v = realloc(lang_v, (lang_n + 1) * sizeof *lang_v);
		if (!lang_k || !lang_v) exit(2);
		lang_k[lang_n] = strdup(line); lang_v[lang_n] = strdup(eq + 1); lang_n++;
	}
	fclose(fp);
}

static const char *tr(const char *key)
{
	for (int i = 0; i < lang_n; i++) if (!strcmp(lang_k[i], key)) return lang_v[i];
	return key;
}

/* columnas = caracteres UTF-8 (la fuente es monoespaciada) */
static int lang_cols(const char *s)
{
	int n = 0;
	for (; *s; s++) if ((*s & 0xC0) != 0x80) n++;
	return n;
}
#endif
