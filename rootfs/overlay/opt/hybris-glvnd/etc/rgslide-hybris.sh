# libhybris (Mali) como EGL por defecto. Lo instala/quita rgslide-gpu-default.
export HYBRIS_LINKER_DIR=/opt/hybris-glvnd/aux/libhybris/linker
export HYBRIS_EGLPLATFORM_DIR=/opt/hybris-glvnd/aux/libhybris
export HYBRIS_EGLPLATFORM=drm
export LIBEGL=/vendor/lib64/egl/libGLES_mali.so
export LIBGLESV2=/vendor/lib64/egl/libGLES_mali.so
export LIBGLESV1_CM=/vendor/lib64/egl/libGLES_mali.so
export HYBRIS_ANDROID_SDK_VERSION=33
export HYBRIS_LD_LIBRARY_PATH=/opt/hybris/alib:/vendor/lib64:/system/lib64:/odm/lib64
case ":${LD_PRELOAD:-}:" in *:/opt/hybris-glvnd/libtlspad.so:*) ;; *) export LD_PRELOAD="/opt/hybris-glvnd/libtlspad.so${LD_PRELOAD:+:$LD_PRELOAD}" ;; esac
