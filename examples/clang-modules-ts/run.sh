#!/bin/dash

# ------------------------------------------------------------ Parse Commandline

CLEAN=
VERBOSE=
CONFIG=asan
TARGET0=a.out
TOOLCHAIN=clang-trunk
NO_BUILD=0
GDB=0
BUILDD=build
BUILD_ONLY=0
BUILD_TESTS=0
LTO=0

TARGET="$TARGET0"

while [ "$#" -gt "0" ] ; do
    # Compiler
    [ "$1" = "clang" ]     && TOOLCHAIN="clang-trunk" && shift && continue

    # Configuration
    [ "$1" = "debug" ]     && CONFIG=debug && shift && continue
    [ "$1" = "gdb" ]       && CONFIG=debug && GDB=1 && shift && continue
    [ "$1" = "release" ]   && CONFIG=release && shift && continue
    [ "$1" = "asan" ]      && CONFIG=asan && shift && continue    # sanitizers
    [ "$1" = "usan" ]      && CONFIG=usan && shift && continue
    
    # Other options
    [ "$1" = "clean" ]     && CLEAN="-t clean" && shift && continue
    [ "$1" = "verbose" ]   && FEEDBACK="-v" && shift && continue # verbose ninja
    [ "$1" = "nf" ]        && FEEDBACK="-q" && shift && continue # quiet ninja
    [ "$1" = "mobius" ]    && NO_BUILD="1" && shift && continue  # no ninja
    [ "$1" = "build" ]     && BUILD_ONLY="1" && shift && continue
    [ "$1" = "test" ]      && BUILD_TESTS="1" && shift && continue
    [ "$1" = "lto" ]       && LTO=1 && shift && continue
    [ "$1" = "no-lto" ]    && LTO=0 && shift && continue
    
    # Target
    [ "$1" = "build.ninja" ] && TARGET=build.ninja && shift && continue
    break
done

# ---------------------------------------------------------------------- Execute

PPWD="$(cd "$(dirname "$0")"; pwd)"
cd "$(dirname "$0")"

export TOOLCHAIN=$TOOLCHAIN
export BUILDDIR="/tmp/build-$USER/${TOOLCHAIN}-${CONFIG}/$TARGET"
export TARGET="$BUILDD/${TOOLCHAIN}-${CONFIG}/$TARGET"
export SRC_DIRECTORIES="src contrib"
export LINK_COMMAND=link_exec
export EXTRA_LINK="\$cli_link_extra"

mkdir -p "$(dirname "$TARGET")"

case $CONFIG in
    release)
        export O_FLAG="-O2"
        export C_FLAGS="\$w_flags \$f_flags \$r_flags"
        export CPP_FLAGS="\$w_flags \$f_flags \$r_flags"
        export LINK_FLAGS="\$l_flags"
        ;;

    debug)
        export O_FLAG="-O0"
        export C_FLAGS="\$w_flags \$f_flags \$d_flags \$gdb_flags"
        export CPP_FLAGS="\$c_flags"
        export LINK_FLAGS="\$l_flags"
        ;;

    asan)
        export O_FLAG="-O0"
        export C_FLAGS="\$w_flags \$f_flags \$d_flags \$asan_flags"
        export CPP_FLAGS="\$c_flags"
        export LINK_FLAGS="\$l_flags \$asan_link"
        ;;

    usan)
        export O_FLAG="-O1"
        export C_FLAGS="\$w_flags \$f_flags \$s_flags \$usan_flags"
        export CPP_FLAGS="\$c_flags"
        export LINK_FLAGS="\$l_flags \$usan_link"
        ;;

    tsan)
        # https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual
        ! [ "$TOOLCHAIN" = "clang-6.0" ] && \
            echo "tsan is only supported with clang." && \
            exit 1
        export O_FLAG="-O2"
        export C_FLAGS="\$w_flags \$f_flags \$s_flags \$tsan_flags"
        export CPP_FLAGS="\$c_flags"
        export LINK_FLAGS="\$l_flags \$tsan_link"
        ;;

    prof)
        export O_FLAG="-O2"
        export C_FLAGS="\$w_flags \$f_flags \$r_flags \$prof_flags"
        export CPP_FLAGS="\$c_flags"
        export LINK_FLAGS="\$l_flags \$prof_link"
        export SRC_DIRECTORIES="src"
        ;;

    analyze)
        ! [ "$TOOLCHAIN" = "clang-trunk" ] \
            echo "Static analysis is only supported with clang." && \
            exit 1
        export O_FLAG="-O2"
        export C_FLAGS="\$w_flags \$f_flags \$r_flags"
        export CPP_FLAGS="\$c_flags"
        export LINK_FLAGS="\$l_flags"
        ;;
    
    *)
        echo "Unexpected config=$CONFIG, aborting"
        exit 1
    ;;
esac

if [ "$LTO" = "1" ] ; then
    export C_FLAGS="-flto=thin $C_FLAGS"
    export CPP_FLAGS="-flto=thin $CPP_FLAGS"
    export LINK_FLAGS="-flto=thin $LINK_FLAGS"
fi

# ---- Run Mobius

if [ "$NO_BUILD" = "1" ] ; then
    mobius -i build-config/build.mobius 
    exit $?
fi

# Ensure that we've got the latest build.ninja file
NINJA_FILE="$(dirname "$TARGET")/build.ninja"
! cat build-config/build.mobius | mobius -m \$moduledir - > "$NINJA_FILE" \
    && exit 1

[ "$TARGET" = "build.ninja" ] && exit 0

! ninja -f "$NINJA_FILE" -C "$PPWD" $FEEDBACK $CLEAN \
    && exit 1

! [ "$CLEAN" = "" ] && exit 0
[ "$BUILD_ONLY" = "1" ] && exit 0

# ---- If we're building the executable (TARGET0), then run it

if [ "$(basename "$TARGET")" = "$TARGET0" ] ; then

    cd "$PPWD"
    PRODUCT="$PPWD/$TARGET"

    if [ "$GDB" = "1" ] ; then
        gdb -ex run -silent -return-child-result -statistics --args "$PRODUCT" "$@"
        exit $?
    fi

    export ASAN_OPTIONS="detect_leaks=1"
    "$PRODUCT" "$@"
    exit $?
    
fi

exit 0

