# gltest
`gltest.c`: EGL+GLES3 offscreen con el blob Mali (dlopen de /vendor/lib64/egl/libGLES_mali.so), vuelca /tmp/gltest.ppm. Compilar: `scripts/build-gltest.sh` -> `out/gltest`; ejecutar con `/system/bin/linker64 out/gltest` (o directo, el interprete ya es linker64).
Vulkan (vkinfo) NO implementado: vulkan.ums9620.so es un modulo HAL (HMI), hay que abrirlo con hw_get_module/hw_module_t y sacar vkGetInstanceProcAddr de hw_vulkan_module_t; no es trivial sin los headers de hardware/vulkan.h.
