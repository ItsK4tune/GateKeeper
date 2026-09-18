# syntax=docker/dockerfile:1

# Stage 1: Build
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy CMake configuration and sources
COPY CMakeLists.txt ./
COPY include/ ./include/
COPY src/ ./src/

# Configure and compile release binaries
RUN cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    && ninja -C build gatekeeper gate \
    && strip build/gatekeeper build/gate

# Stage 2: Runtime
FROM ubuntu:24.04 AS runner

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Create non-root system user and working directory
RUN groupadd -r -g 10001 gatekeeper \
    && useradd -r -u 10001 -g gatekeeper -s /bin/false -d /app gatekeeper \
    && mkdir -p /var/log/gatekeeper /app \
    && chown -R gatekeeper:gatekeeper /var/log/gatekeeper /app

# Copy binaries from builder
COPY --from=builder /app/build/gatekeeper /usr/local/bin/gatekeeper
COPY --from=builder /app/build/gate /usr/local/bin/gate

WORKDIR /app
USER gatekeeper:gatekeeper

# Expose GKWP (63779) and HTTP API (8080)
EXPOSE 63779 8080

HEALTHCHECK --interval=10s --timeout=3s --start-period=3s --retries=3 \
  CMD curl -f http://localhost:8080/healthz || exit 1

ENTRYPOINT ["/usr/local/bin/gatekeeper"]
CMD ["--port", "63779", "--http-port", "8080", "--log", "terminal"]