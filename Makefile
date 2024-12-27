#CFLAGS = -Wall -Wextra -Wformat=2 -Wconversion -Wimplicit-fallthrough -Werror
#CFLAGS = -Wall -Wextra -Wformat=2 -Wconversion -Wimplicit-fallthrough
TMPDIR = /tmp/ubuntu-image
ROOTFS = ${HOME}/ubuntu-rootfs

all: nstest

.PHONY: clean
clean:
	rm -f *.o nstest

.PHONY: rootfs
rootfs:
	mkdir -p $(TMPDIR)
	podman pull ubuntu:latest
	podman save ubuntu:latest | tar -C $(TMPDIR) -xpf -
	mkdir -p $(ROOTFS)
	tar -C $(ROOTFS) -xpf $(TMPDIR)/$$(jq -r '.[].Layers[0]' $(TMPDIR)/manifest.json)

nstest: main.o
	gcc -o $@ $^

main.o: main.c
	gcc $(CFLAGS) -o $@ -c $^
