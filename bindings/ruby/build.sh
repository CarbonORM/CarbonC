#!/usr/bin/env bash

set -eEBx

rm -f \
    Makefile \
    carbon.bundle \
    carbon.o \
    carbon_ruby.o \
    mkmf.log

ruby extconf.rb
make
