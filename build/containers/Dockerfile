# SPDX-License-Identifier: MIT

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
      ca-certificates \
      cmake \
      g++ \
      git \
      make \
      python3 \
      python3-venv \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace/openAUTOSAR
