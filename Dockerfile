FROM ghcr.io/wiiu-env/devkitppc:20260225@sha256:472118aa3d2a1ebb62691e9ae18a21d4d8f14238d688d679c30f5973dc9d9f35

RUN apt-get update && apt-get install -y --no-install-recommends python3 g++ \
    && rm -rf /var/lib/apt/lists/*
ENV PYTHONUNBUFFERED=1 PYTHONDONTWRITEBYTECODE=1
WORKDIR /project
ENTRYPOINT ["python3", "/project/tools/container_build.py"]
