/* LD_PRELOAD de depuracion (glibc, aarch64): al recibir SIGSEGV/SIGBUS/SIGABRT imprime PC, LR, SP, la direccion y el mapa de memoria de PC/LR a stderr. */
#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

static void show(const char *tag, unsigned long a)
{
	FILE *f = fopen("/proc/self/maps", "r"); char l[512];
	while (f && fgets(l, sizeof l, f)) {
		unsigned long lo, hi, off;
		if (sscanf(l, "%lx-%lx %*s %lx", &lo, &hi, &off) == 3 && a >= lo && a < hi) { fprintf(stderr, "SEGVTRACE %s=%#lx en %s (off lib=%#lx)", tag, a, strchr(l, '/') ? strchr(l, '/') : "?\n", a - lo + off); fflush(stderr); if (!strchr(l, '/')) fputc('\n', stderr); fclose(f); return; }
	}
	fprintf(stderr, "SEGVTRACE %s=%#lx sin mapa\n", tag, a);
	if (f) fclose(f);
}

static void h(int sig, siginfo_t *si, void *uc)
{
	ucontext_t *u = uc;
	unsigned long pc = u->uc_mcontext.pc, lr = u->uc_mcontext.regs[30], sp = u->uc_mcontext.sp;
	fprintf(stderr, "SEGVTRACE sig=%d addr=%p pc=%#lx lr=%#lx sp=%#lx\n", sig, si->si_addr, pc, lr, sp);
	show("pc", pc); show("lr", lr);
	unsigned long *s = (unsigned long *)sp;
	for (int i = 0; i < 24; i++) { unsigned long v = s[i]; if (v > 0x7000000000UL && v < 0x8000000000UL) show("stk", v); }
	_exit(139);
}

__attribute__((constructor)) static void init(void)
{
	static char stk[65536];
	stack_t ss = { .ss_sp = stk, .ss_size = sizeof stk };
	sigaltstack(&ss, 0);
	struct sigaction sa; memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = h; sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigaction(SIGSEGV, &sa, 0); sigaction(SIGBUS, &sa, 0);
}
