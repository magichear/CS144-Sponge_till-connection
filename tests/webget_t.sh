#!/bin/bash

WEB_HASH=`./apps/webget cs144.keithw.org /hasher/xyzzy | tee /dev/stderr | tail -n 1`
/headless/sponge/tap.sh start
service apache2 start
echo "QWx0NhMPkoM/bJr/ohvHXlviFhOyYrYb+qqdOnwLYo4"  > /var/www/html/xyzzy
WEB_HASH=`./apps/webget 169.254.10.1 /xyzzy | tee /dev/stderr | tail -n 1`
CORRECT_HASH="QWx0NhMPkoM/bJr/ohvHXlviFhOyYrYb+qqdOnwLYo4"

if [ "${WEB_HASH}" != "${CORRECT_HASH}" ]; then