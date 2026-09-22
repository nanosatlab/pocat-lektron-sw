FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

ENV POCAT_PSK="0123456789ABCDEF0123456789ABCDEF"

RUN apt-get update && apt-get install -y \
    wget \
    tar \
    make \
    cmake \
    git \
    xz-utils \
    python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt
RUN wget https://developer.arm.com/-/media/Files/downloads/gnu/13.2.rel1/binrel/arm-gnu-toolchain-13.2.rel1-x86_64-arm-none-eabi.tar.xz \
    && tar -xvf arm-gnu-toolchain-13.2.rel1-x86_64-arm-none-eabi.tar.xz \
    && mv arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi arm-gcc \
    && rm arm-gnu-toolchain-13.2.rel1-x86_64-arm-none-eabi.tar.xz

ENV PATH="/opt/arm-gcc/bin:${PATH}"

WORKDIR /app

CMD ["bash","build.sh"]