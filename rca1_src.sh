#!/bin/sh

IN="in0_direct"

case "$1" in
    on)
        echo "RCA2 capture"
        echo 1 > /tmp/mute
        {
            alsaloop -C $IN --rate=96000 -f S32_LE -P default -t 30000 -S 3
        } >/dev/null 2>&1 &
        exit 0
        ;;

    off)
        killall alsaloop
        #killall `basename $0`
        exit 0
        ;;

    stop)
        killall alsa_signal
        killall alsaloop
        #killall `basename $0`
        exit 0
        ;;

    listen)
        killall alsa_signal
        {
            ./alsa_signal $IN s32le:44100:2:128:500000 17.0 2:8 "./$(basename $0) on" "./$(basename $0) off"
        } >/dev/null 2>&1 &
        exit 0
        ;;
esac