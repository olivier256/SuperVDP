#!/bin/sh
# =========================================================================
# Lance le banc sh2cycles dans un conteneur Linux (Docker Desktop, OrbStack
# ou colima sur Mac), depuis la racine du dépôt :
#     tools/sh2cycles/docker.sh
# La première fois, construit l'image (chaîne sh-elf et qemu : environ
# 40 minutes). Ensuite, le banc prend quelques secondes. Les résultats
# sont dans build/sh2cycles, comme sous Linux.
# =========================================================================
set -e

IMAGE=dds32-sh2cycles

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "Construction de l'image $IMAGE (une seule fois, environ 40 minutes)..."
    docker build -t "$IMAGE" tools/sh2cycles
fi

docker run --rm -v "$PWD":/src "$IMAGE"
