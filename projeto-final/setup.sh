tar xf fuse-3.2.1.tar.xz
cp passthrough.c fuse-3.2.1/example/
mkdir fuse-3.2.1/build
cd fuse-3.2.1/build/
meson ..
