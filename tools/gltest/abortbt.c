#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ucontext.h>
#include <stdint.h>

static void where(uintptr_t pc)
{
	FILE *f = fopen("/proc/self/maps", "r");
	char l[512];
	while (f && fgets(l, sizeof l, f)) {
		unsigned long a, b, off;
		char perm[8], path[400] = "";
		if (sscanf(l, "%lx-%lx %7s %lx %*s %*s %399s", &a, &b, perm, &off, path) >= 4 && pc >= a && pc < b) {
			dprintf(2, "  pc=0x%lx  %s+0x%lx\n", (unsigned long)pc, path, (unsigned long)(pc - a + off));
			fclose(f);
			return;
		}
	}
	if (f) fclose(f);
	dprintf(2, "  pc=0x%lx  (?)\n", (unsigned long)pc);
}

static void h(int sig, siginfo_t *si, void *uc_)
{
	ucontext_t *uc = uc_;
	uintptr_t pc = uc->uc_mcontext.pc, fp = uc->uc_mcontext.regs[29], lr = uc->uc_mcontext.regs[30];
	dprintf(2, "ABORTBT sig=%d\n", sig);
	where(pc);
	where(lr);
	for (int i = 0; i < 24 && fp && !(fp & 7); i++) {
		uintptr_t *f = (uintptr_t *)fp;
		uintptr_t nfp = f[0], ret = f[1];
		if (!ret) break;
		where(ret);
		if (nfp <= fp) break;
		fp = nfp;
	}
	_exit(134);
}

__attribute__((constructor)) static void init(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = h;
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigaction(SIGABRT, &sa, 0);
	sigaction(SIGSEGV, &sa, 0);
}
