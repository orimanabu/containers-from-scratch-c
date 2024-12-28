# containers-from-scratch-c

This is my playground to learn Linux namespaces.

Basically, this program (`nstest`) creates User namespace and UTS/PID/Mount namespaces, then run specified commands in the namespaces.

## Build

```
make
```

## Prepare for rootfs

Extract a container image you like, or

```
make rootfs
```

## Run process in user namespace

```
./nstest bash
```
