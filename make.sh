tar xf fuse-3.11.0.tar.xz
cp passthrough.c fuse-3.11.0/example/
mkdir fuse-3.11.0/build
cd fuse-3.11.0/build/
meson ..
cd fuse-3.11.0/build/
ninja
./example/passthrough -f example/50d858e@@passthrough@exe/


