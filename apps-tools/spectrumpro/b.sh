rw
cmake -B./build -DINSTALL_DIR=/opt/redpitaya  -DCMAKE_BUILD_TYPE=Debug
make -C build install -j2
#cp -rf ./build/spectrumpro/ /opt/spectrumpro/www/apps/