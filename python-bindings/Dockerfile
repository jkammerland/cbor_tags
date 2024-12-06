FROM fedora:41

# Install necessary packages
RUN dnf update -y && dnf install -y \
    git \
    cmake \
    gcc \
    gcc-c++ \
    ninja-build \
    python3 \
    make \
    which \
    perl \
    tar \
    xz \
    python-devel \
    ncurses-devel \
    findutils \
    && dnf clean all

# Create working directory
WORKDIR /llvm-project

# Clone LLVM project
RUN git clone https://github.com/llvm/llvm-project.git . && \
    git checkout llvmorg-19.1.4

# Create build directory
RUN mkdir build && cd build

# Configure and build LLVM/Clang with RTTI enabled
WORKDIR /llvm-project/build
RUN cmake -G Ninja ../llvm \
    -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra" \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_BUILD_TESTS=ON \
    -DLLVM_ENABLE_RTTI=ON \
    -DLLVM_ENABLE_EH=ON \
    && ninja && ninja install

# Clean up
RUN cd / && rm -rf /llvm-project

# Install cbor_tags
WORKDIR /tmp
RUN git clone https://github.com/jkammerland/cbor_tags.git && \
    cd cbor_tags && \
    git checkout python-bindings-gen-bug-fix && \
    mkdir build && cd build && \
    cmake -G Ninja .. -DBUILD_TESTS=ON -DBUILD_PYTHON_BINDINGS=ON && \
    ninja

# Default command
CMD ["/bin/bash"]