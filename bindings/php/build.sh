#!/usr/bin/env bash

set -eEBx

phpize
./configure
make
make install

