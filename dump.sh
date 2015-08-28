#!/bin/sh
bin="sos"
if [ -n "$1" ]
then
    bin="$1"
fi

arm-none-linux-gnueabi-objdump -Dlx stage/arm/imx6/bin/"$bin" | less
