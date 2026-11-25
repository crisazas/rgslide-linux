# Entorno de compilacion arm64 con la misma glibc que la Slide (Debian trixie). Se ejecuta con qemu-user en el PC.
FROM debian:trixie
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential autoconf automake libtool pkg-config git ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
