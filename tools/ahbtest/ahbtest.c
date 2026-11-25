#include <android/hardware_buffer.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
	AHardwareBuffer_Desc d, o;
	AHardwareBuffer *b = 0;
	void *p = 0;
	int r;

	memset(&d, 0, sizeof d);
	d.width = 256; d.height = 256; d.layers = 1;
	d.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
	d.usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN |
		  AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
	r = AHardwareBuffer_allocate(&d, &b);
	printf("allocate: rc=%d buf=%p\n", r, (void *)b);
	fflush(stdout);
	if (r || !b) return 2;
	AHardwareBuffer_describe(b, &o);
	printf("describe: %ux%u stride=%u fmt=%u usage=0x%llx\n", o.width, o.height, o.stride, o.format, (unsigned long long)o.usage);
	r = AHardwareBuffer_lock(b, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, 0, &p);
	printf("lock: rc=%d ptr=%p\n", r, p);
	if (!r && p) { memset(p, 0x7f, 4096); AHardwareBuffer_unlock(b, 0); printf("escritura+unlock OK\n"); }
	AHardwareBuffer_release(b);
	printf("FIN\n");
	return r ? 3 : 0;
}
