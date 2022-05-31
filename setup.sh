tar xf fuse-3.11.0.tar.xz
cp passthrough.c fuse-3.11.0/example/
mkdir fuse-3.11.0/build
cd fuse-3.11.0/build/
meson ..
