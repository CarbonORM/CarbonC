#!/usr/bin/env bash

set -eEBx

rm -rf \
    .deps \
    .libs \
    autom4te.cache \
    build \
    include \
    modules

rm -f \
    Makefile \
    Makefile.fragments \
    Makefile.global \
    Makefile.objects \
    acinclude.m4 \
    aclocal.m4 \
    .dep \
    .lo \
    *.dep \
    *.la \
    *.lo \
    *.loT \
    *~ \
    config.cache \
    config.guess \
    config.h \
    config.h.in \
    config.log \
    config.nice \
    config.status \
    config.sub \
    configure \
    configure.ac \
    libtool \
    run-tests.php

phpize
./configure --enable-carbon
make
