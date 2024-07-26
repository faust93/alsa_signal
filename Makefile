CC = gcc
LD = gcc
CFLAGS = -Wall -Werror
LDFLAGS = -lasound

EXE = alsa_signal

$(EXE): $(EXE).c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(EXE)
