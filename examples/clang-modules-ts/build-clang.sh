#!/bin/bash

set -e

# -------------------------------------------------------------------- variables

TMPD=$(mktemp -d /tmp/$(basename $0).XXXXX)
trap cleanup EXIT
cleanup()
{
    rm -rf $TMPD
}

NP=$(expr 3 \* `nproc` / 2)

# ------------------------------------------------ ensure packages are installed

sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
sudo apt-get update
sudo apt-get install -y wget subversion automake \
     clang-6.0 clang-6.0 clang-format-6.0 clang-tidy-6.0 lld-6.0 lldb-6.0 clang-tools-6.0

build_clang()
{
    CLANG_V="$1"
    TAG="$2"
    LLVM_DIR="llvm-${TAG}"

    SRC_D=$TMPD/$LLVM_DIR
    BUILD_D=$TMPD/$LLVM_DIR/build
    rm -rf "$SRC_D"
    mkdir -p "$SRC_D"
    mkdir -p "$BUILD_D"

    cd $SRC_D
    nice ionice -c3 svn co http://llvm.org/svn/llvm-project/llvm/$CLANG_V llvm

    cd $SRC_D/llvm/projects
    nice ionice -c3 svn co http://llvm.org/svn/llvm-project/libunwind/$CLANG_V libunwind
    nice ionice -c3 svn co http://llvm.org/svn/llvm-project/libcxx/$CLANG_V libcxx
    nice ionice -c3 svn co http://llvm.org/svn/llvm-project/libcxxabi/$CLANG_V libcxxabi
    nice ionice -c3 svn co http://llvm.org/svn/llvm-project/compiler-rt/$CLANG_V compiler-rt
    nice ionice -c3 svn co http://llvm.org/svn/llvm-project/test-suite/$CLANG_V test-suite

    cd $SRC_D/llvm/tools
    nice ionice -c3 svn co http://llvm.org/svn/llvm-project/cfe/$CLANG_V clang
    nice ionice -c3 svn co http://llvm.org/svn/llvm-project/lld/$CLANG_V lld

    cd $SRC_D/llvm/tools/clang/tools
    nice ionice -c3 svn co http://llvm.org/svn/llvm-project/clang-tools-extra/$CLANG_V extra

    cd $BUILD_D

    nice ionice -c3 cmake -G "Unix Makefiles" \
         -DCMAKE_BUILD_TYPE=release \
         -DCMAKE_C_COMPILER=clang-6.0 \
         -DCMAKE_CXX_COMPILER=clang++-6.0 \
         -DLLVM_ENABLE_ASSERTIONS=Off \
         -DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=Yes \
         -DLIBCXX_ENABLE_SHARED=YES \
         -DLIBCXX_ENABLE_STATIC=YES \
         -DLIBCXX_ENABLE_FILESYSTEM=YES \
         -DLIBCXX_ENABLE_EXPERIMENTAL_LIBRARY=YES \
         -LLVM_ENABLE_LTO=thin \
         -LLVM_USE_LINKER=lld-6.0 \
         -DCMAKE_INSTALL_PREFIX:PATH=/opt/llvm/$TAG \
         $SRC_D/llvm

    /usr/bin/time -v nice ionice -c3 make -j$NP 2>$BUILD_D/stderr.text | tee $BUILD_D/stdout.text
    sudo make install | tee -a $BUILD_D/stdout.text
    cat $BUILD_D/stderr.text
}

# ------------------------------------------------------------------------ build

build_clang trunk trunk
#build_clang tags/RELEASE_700/final 7.0
#build_clang tags/RELEASE_601/final 6.0


