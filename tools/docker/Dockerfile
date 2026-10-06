# SPDX-License-Identifier: GPL-3.0-only
# The JieLi pi32v2 toolchain (Linux x86-64) for macOS, Windows and Linux arm64 (BUILDING.md, "Docker").
#
#   python tools/optimist.py setup            builds this image when the host cannot run the toolchain
#   docker build --platform linux/amd64 -t optimist-toolchain:20250324.1 tools/docker
#
# The toolchain is downloaded from JieLi's own server while the image builds, on the user's machine. It carries
# no licence that allows passing it on (BUILDING.md, "The toolchain's licence"): never push this image to a
# registry. A copy you downloaded yourself works without an image: JIELI_TOOLCHAIN=<dir>.
#
# The image also holds the three AC79 SDK files (Apache-2.0) the package needs, Python with Pillow (pinned: the
# generated font must come out the same everywhere) and gcc for the host tests. Build it for linux/amd64 (the
# toolchain's only platform; Rosetta or QEMU run it on arm64 hosts).
FROM debian:bookworm-slim

ARG TOOLCHAIN_VERSION=20250324.1
ARG TOOLCHAIN_URL=https://jl-update.oss-cn-shenzhen.aliyuncs.com/jieli-linux-toolchains-${TOOLCHAIN_VERSION}.tar.xz
ARG TOOLCHAIN_SHA256=f686586bcfb45e0f0bb27fd2b39c7a7f313cb4f0e88a66a14da621ffa8225958
ARG SDK_TAG=AC79NN_SDK_V1.2.1_2023-12-13
ARG PILLOW_VERSION=12.2.0

RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates curl xz-utils python3 python3-venv gcc libc6-dev git \
 && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL --retry 3 --connect-timeout 30 --max-time 300 -o /tmp/tc.tar.xz "$TOOLCHAIN_URL" \
 && echo "$TOOLCHAIN_SHA256  /tmp/tc.tar.xz" | sha256sum -c - \
 && tar -xJf /tmp/tc.tar.xz -C /opt \
 && mv /opt/jieli-linux-toolchains-* /opt/jieli \
 && rm /tmp/tc.tar.xz \
 && /opt/jieli/pi32v2/bin/clang --version >/dev/null

RUN base="https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK/raw/$SDK_TAG/cpu/wl82/tools" \
 && mkdir -p /opt/ac79/cpu/wl82/tools/cfg && cd /opt/ac79/cpu/wl82/tools \
 && for f in uboot.boot cfg_tool.bin cfg/eq_cfg_hw.bin; do curl -fsSL --retry 3 --connect-timeout 30 --max-time 300 -o "$f" "$base/$f"; done \
 && printf '%s  %s\n' \
      4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4 uboot.boot \
      276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b cfg_tool.bin \
      41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d cfg/eq_cfg_hw.bin | sha256sum -c -

RUN python3 -m venv /opt/venv && /opt/venv/bin/pip install --no-cache-dir --disable-pip-version-check "Pillow==$PILLOW_VERSION"

ENV PATH=/opt/venv/bin:$PATH \
    JIELI_TOOLCHAIN=/opt/jieli \
    AC79_SDK=/opt/ac79 \
    OPTIMIST_IN_CONTAINER=1
LABEL org.opencontainers.image.description="JieLi pi32v2 toolchain for the Optimist firmware: local use only, do not push"
WORKDIR /work
