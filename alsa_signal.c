/*
 *  ALSA Input Signal Hook by Faust93 <monumentum@gmail.com>
 *  Based on alsa2pipe (https://github.com/004helix/alsa2pipe) by Raman Shyshniou <rommer@ibuffed.com>
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 *  USA.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <alsa/asoundlib.h>


char *onconnect = NULL;
char *ondisconnect = NULL;

double silenceth = 20.0;
long probe_delay = 1000000;
int probe_num_sound = 3;
int probe_num_silence = 10;

void runhook(char *prog)
{
    char *argv[3];
    char cmd[256];
    char arg[256];

    pid_t pid = fork();

    switch (pid) {
        case -1:
            fprintf(stderr, "exec failed, fork: %s\n", strerror(errno));
            return;
        case 0:
            int ret = sscanf(prog,"%s %s", cmd, arg);
            argv[0] = cmd;
            if (ret == 2) {
                argv[1] = arg;
                argv[2] = NULL;
            } else {
                argv[1] = NULL;
            }
            fprintf(stderr, "Spawning hook: %s %s\n", argv[0], argv[1]);
            execvp(cmd, argv);
            fprintf(stderr, "execvp failed: %s\n", strerror(errno));
            exit(1);
        break;
    }
}


void run(snd_pcm_t *handle, void *buffer, long frames,
         snd_pcm_format_t format, unsigned channels)
{
    snd_pcm_sframes_t size;
    int silence = 1;
    long power = 0;
    int over_thresh = 0;
    int below_thresh = 0;
    double db = 0;
    int fmt = snd_pcm_format_width(format);

    long skip_first = frames + 1;

    for (;;) {
        size = snd_pcm_readi(handle, buffer, frames);

        if (size == 0)
            continue;

        if (skip_first != 0) {
            skip_first--;
            continue;
        }

        if (size > 0) {
            power = 0;
            if(fmt == 32) {
                int32_t *data = buffer;
                for (int i = 0; i < size / 2; i++)
                    power += pow(data[i], 2) + pow(data[i + 1], 2);
                power = sqrt(power/size);
            } else if(fmt == 16) {
                int16_t *data = buffer;
                for (int i = 0; i < size / 2; i++)
                    power += pow(data[i], 2) + pow(data[i + 1], 2);
                power = sqrt(power/size);
            } else if(fmt == 8) {
                int8_t *data = buffer;
                for (int i = 0; i < size / 2; i++)
                    power += pow(data[i], 2) + pow(data[i + 1], 2);
                power = sqrt(power/size);
            }

            db = 10 * log10(power / 32768);

            if (db >= silenceth && power != 0){
                over_thresh++;
                over_thresh = over_thresh % 1024;
                below_thresh = 0;
            } else {
                below_thresh++;
                over_thresh = 0;
            }

            if (over_thresh >= probe_num_sound && silence) {
                fprintf(stderr, "Input sound detected\n");
                over_thresh = 0;
                silence = 0;

                if (onconnect)
                    runhook(onconnect);
            }

            if (below_thresh >= probe_num_silence && silence == 0) {
                fprintf(stderr, "Silence detected\n");
                below_thresh = 0;
                over_thresh = 0;
                silence = 1;

                if (ondisconnect)
                    runhook(ondisconnect);
            }

            // Uncomment the string below for proper threshold tuning on your hardware
            //fprintf(stderr,"db:%f pow:%ld\n",db, power);

        }

        usleep(probe_delay);
        snd_pcm_prepare(handle);

        int avail;
        while (1) {
            avail = snd_pcm_avail(handle);
            if (avail == -EAGAIN)
                continue;
            break;
        }

    }
}


int main(int argc, char *argv[])
{
    char *buffer;
    long frames = 128;
    unsigned rate, channels;
    snd_pcm_t *capture_handle;
    snd_pcm_hw_params_t *hw_params;
    snd_pcm_format_t format;
    char sformat[32];
    size_t bufsize;
    int err, i;

    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);

    // check command line arguments
    if (argc < 5) {
        fprintf(stderr,
                "Usage:\n %s <device> <format> <silence_threshold> <probe_num_detected:probe_num_silence> [sound_detected_hook_cmd] [silence_hook_cmd]\n"
                "  format: <sample-format:sample-rate:channels[:buffer:probe_delay]>\n"
                "    sample-format: u8, s8, s16le, s16be\n"
                "                   s24le, s24be, s32le, s32be\n"
                "    sample-rate: 48000, 44100, ...\n"
                "    channels: 4, 2, 1, ...\n"
                "    buffer: buffer duration is # frames (128)\n"
                "    probe_delay: input source probe delay in microseconds (1000000 = 1s)\n"
                "  silence_threshold: Silence threshold (20.0)\n"
                "  probe_num: # of matching threshold probes before launching hooks\n",
                argv[0]);
        return 1;
    }

    bufsize = 0;
    buffer = strdup(argv[2]);
    for (i = 0; buffer[i]; i++) {
        bufsize++;
        if (buffer[i] == ':')
            buffer[i] = ' ';
    }

    if (bufsize >= sizeof(sformat)) {
        fprintf(stderr, "format option too long\n");
        return 1;
    }

    if (sscanf(buffer, "%s %u %u %ld %ld",
               sformat, &rate, &channels, &frames, &probe_delay) < 3) {
        fprintf(stderr, "unknown format: %s\n", argv[2]);
        free(buffer);
        return 1;
    }

    free(buffer);

    if (!strcmp(sformat, "s8")) {
        format = SND_PCM_FORMAT_S8;
    } else
    if (!strcmp(sformat, "u8")) {
        format = SND_PCM_FORMAT_U8;
    } else
    if (!strcmp(sformat, "s16le")) {
        format = SND_PCM_FORMAT_S16_LE;
    } else
    if (!strcmp(sformat, "s16be")) {
        format = SND_PCM_FORMAT_S16_BE;
    } else
    if (!strcmp(sformat, "s24le")) {
        format = SND_PCM_FORMAT_S24_LE;
    } else
    if (!strcmp(sformat, "s24be")) {
        format = SND_PCM_FORMAT_S24_BE;
    } else
    if (!strcmp(sformat, "s32le")) {
        format = SND_PCM_FORMAT_S32_LE;
    } else
    if (!strcmp(sformat, "s32be")) {
        format = SND_PCM_FORMAT_S32_BE;
    } else {
        fprintf(stderr, "unknown frame format: %s\n", sformat);
        return 1;
    }

    if (frames <= 0) {
        fprintf(stderr, "unknown frame format: %s\n", sformat);
        return 1;
    }

    if (probe_delay <= 0) {
        fprintf(stderr, "unknown probe delay format: %s\n", sformat);
        return 1;
    }

    silenceth = atof(argv[3]);
    if (silenceth <= 0 ) {
        fprintf(stderr, "invalid silence threshold\n");
    }

    bufsize = 0;
    buffer = strdup(argv[4]);
    for (i = 0; buffer[i]; i++) {
        bufsize++;
        if (buffer[i] == ':')
            buffer[i] = ' ';
    }
    if (sscanf(buffer, "%i %i", &probe_num_sound, &probe_num_silence) < 2) {
        fprintf(stderr, "invalid probe_num format: %s\n", argv[4]);
        free(buffer);
        return 1;
    }
    free(buffer);


    // setup connect/disconnect handlers
    if (argc > 5)
        onconnect = strdup(argv[5]);

    if (argc > 6)
        ondisconnect = strdup(argv[6]);

    // open alsa device
    if ((err = snd_pcm_open(&capture_handle, argv[1], SND_PCM_STREAM_CAPTURE, 0 )) < 0) {
        fprintf(stderr, "cannot open audio device %s (%s)\n",
                argv[1], snd_strerror(err));
        return 1;
    }

    if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0) {
        fprintf(stderr, "cannot allocate hardware parameter structure (%s)\n",
                snd_strerror(err));
        return 1;
    }

    if ((err = snd_pcm_hw_params_any(capture_handle, hw_params)) < 0) {
        fprintf(stderr, "cannot initialize hardware parameter structure (%s)\n",
                snd_strerror(err));
        return 1;
    }

    if ((err = snd_pcm_hw_params_set_access(capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        fprintf(stderr, "cannot set access type (%s)\n",
                snd_strerror(err));
        return 1;
    }

    if ((err = snd_pcm_hw_params_set_format(capture_handle, hw_params, format)) < 0) {
        fprintf(stderr, "cannot set sample format (%s)\n",
                snd_strerror(err));
        return 1;
    }

    if ((err = snd_pcm_hw_params_set_rate_near(capture_handle, hw_params, &rate, 0)) < 0) {
        fprintf(stderr, "cannot set sample rate (%s)\n",
                snd_strerror(err));
        return 1;
    }

    if ((err = snd_pcm_hw_params_set_channels(capture_handle, hw_params, channels)) < 0) {
        fprintf(stderr, "cannot set channel count (%s)\n",
                snd_strerror(err));
        return 1;
    }

    if ((err = snd_pcm_hw_params(capture_handle, hw_params)) < 0) {
        fprintf(stderr, "cannot set parameters (%s)\n",
                snd_strerror(err));
        return 1;
    }

    snd_pcm_hw_params_free(hw_params);

    if ((err = snd_pcm_prepare(capture_handle)) < 0) {
        fprintf(stderr, "cannot prepare audio interface for use (%s)\n",
                snd_strerror(err));
        return 1;
    }

    bufsize = frames * channels * (snd_pcm_format_width(format) >> 3);
    if ((buffer = malloc(bufsize)) == NULL) {
        fprintf(stderr, "cannot allocate memory\n");
        return 1;
    }

    run(capture_handle, buffer, frames, format, channels);

    free(buffer);

    snd_pcm_close(capture_handle);

    return 0;
}
