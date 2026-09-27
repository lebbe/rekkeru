#!/usr/bin/env bash
set -euo pipefail

IMAGE=ghcr.io/lebbe/rekkeru:latest
NAME=rekkeru
DIR="$(cd "$(dirname "$0")" && pwd)"

docker pull "$IMAGE"
docker rm -f "$NAME" 2>/dev/null || true
docker run -d --restart unless-stopped \
  --name "$NAME" \
  --env-file "$DIR/.env" \
  -p 127.0.0.1:3000:3000 \
  "$IMAGE"
docker image prune -f
