# ALSA input signal detector

`alsa_signal` listens to ALSA capture device with specified interval and launches external on/off hooks if signal threshold met

### Usecase

Having `alsaloop` constantly running could be quite CPU consuming especially for SBC devices
Instead we can run `alsa_signal` as an initial listener which in turn will start/stop `alsaloop` if there's a signal changes meet the thresholds on the capture device (see `rca1_src.sh` for ex.)


# Usage

```
Usage: alsa_signal <device> <format> <silence_threshold> <probe_num_detected:probe_num_silence> [sound_detected_hook_cmd] [silence_hook_cmd]
       format: <sample-format:sample-rate:channels[:buffer:probe_delay]>
               sample-format: u8, s8, s16le, s16be, s24le, s24be, s32le, s32be
               sample-rate: 48000, 44100, ...
               channels: 4, 2, 1, ...
               buffer: buffer duration is # frames (128)
               probe_delay: input source probe delay (interval) in microseconds (1000000 = 1s)
       silence_threshold: Silence threshold dB (20.0)
       probe_num_detected: # of matching >= threshold probes before launching signal detected hook
       probe_num_silence: # of matching < threshold probes before launching silence hook
```

# Example

```
#!/bin/sh

IN="in0_direct"

case "$1" in
    on)
        echo "RCA1 capture"
        {
            alsaloop -C $IN --rate=96000 -f S32_LE -P default -t 30000 -S 3
        } >/dev/null 2>&1 &
        ;;

    off)
        killall alsaloop
        ;;

    stop)
        killall alsa_signal
        killall alsaloop
        ;;

    listen)
        {
            ./alsa_signal $IN s32le:44100:2:128:500000 17.0 2:8 "./$(basename $0) on" "./$(basename $0) off"
        } >/dev/null 2>&1 &
        ;;
esac
```

> Based on alsa2pipe (https://github.com/004helix/alsa2pipe) by Raman Shyshniou <rommer@ibuffed.com>
