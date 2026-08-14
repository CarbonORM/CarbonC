#!/usr/bin/env bash

set -eEBx

NODE_INCLUDE="$(
    node -e "const path = require('path'); process.stdout.write(path.resolve(path.dirname(process.execPath), '..', 'include', 'node'));"
)"

if [ ! -f "$NODE_INCLUDE/node_api.h" ]; then
    echo "node_api.h not found under $NODE_INCLUDE" >&2
    exit 1
fi

rm -rf build
mkdir -p build

cc -std=c99 -O2 -fPIC \
    -I../../include \
    -I"$NODE_INCLUDE" \
    -c ../../src/carbon.c \
    -o build/carbon.o

c++ -std=c++17 -O2 -fPIC \
    -DNAPI_VERSION=8 \
    -I../../include \
    -I"$NODE_INCLUDE" \
    -c carbon_node.cpp \
    -o build/carbon_node.o

case "$(uname -s)" in
    Darwin)
        c++ -bundle -undefined dynamic_lookup \
            -o build/carbon.node \
            build/carbon_node.o \
            build/carbon.o
        ;;
    *)
        c++ -shared \
            -o build/carbon.node \
            build/carbon_node.o \
            build/carbon.o
        ;;
esac
