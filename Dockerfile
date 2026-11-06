# Build stage
FROM debian:trixie-slim AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates \
      cmake \
      make \
      g++ \
      git \
      libpq-dev \
      libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY server/ ./server/

RUN cmake -S server -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTING=OFF \
    && cmake --build build --parallel

# Runtime stage
FROM debian:trixie-slim AS runtime

ENV DEBIAN_FRONTEND=noninteractive \
    PORT=8443 \
    SRS_BIND=0.0.0.0 \
    SRS_WEB_ROOT=/app/web

RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates \
      curl \
      libpq5 \
      libssl3t64 \
      openssl \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --system --gid 10001 srs \
    && useradd --system --uid 10001 --gid srs \
         --home-dir /app --no-create-home srs \
    && mkdir -p /app/web /run/srs/tls \
    && chown -R srs:srs /app /run/srs

WORKDIR /app

COPY --from=build --chown=srs:srs /src/build/srs_server /app/srs_server
COPY --chown=srs:srs web/ /app/web/

USER srs
EXPOSE 8443

HEALTHCHECK --interval=15s --timeout=5s --start-period=10s --retries=5 \
  CMD curl --insecure --fail --silent https://127.0.0.1:8443/health >/dev/null || exit 1

ENTRYPOINT ["/app/srs_server"]
