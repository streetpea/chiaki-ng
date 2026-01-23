# Dockerfile for building chiaki-ng (Qt6/Linux)
FROM docker.io/stateoftheartio/qt6:6.7-gcc-aqt

# Install build dependencies
RUN sudo apt-get update && sudo apt-get install -y \
    git \
    cmake \
    make \
    g++ \
    ninja-build \
    pkg-config \
    ffmpeg \
    libopus-dev \
    libssl-dev \
    python3-protobuf \
    protobuf-c-compiler \
    protobuf-compiler \
    libsdl2-dev \
    libevdev-dev \
    libsystemd-dev \
    libspeexdsp-dev \
    libjson-c-dev \
    libminiupnpc-dev \
    libfftw3-dev \
    libhidapi-dev \
    && sudo apt-get clean

# Set workdir and copy source
WORKDIR /opt/chiaki-ng
COPY . .
RUN sudo chown -R $(id -u) /opt/chiaki-ng
RUN git config --global --add safe.directory /opt/chiaki-ng
RUN git submodule update --init

# Build
RUN mkdir build && cd build && cmake -DCMAKE_PREFIX_PATH=/opt/Qt/${QT_VERSION}/${GCC_STRING} .. && make -j$(nproc)

# Default command
CMD ["bash"]
