
# Getting Started with C++ Modules in Clang #

If you want to try the new C++ [modules-ts](wg21.link/n4720) with [clang](https://clang.llvm.org/), read on.

[Mobius](https://github.com/aaron-michaux/mobius) is a preprocessor for the [ninja build system](https://ninja-build.org/) that discovers [build statements](https://ninja-build.org/manual.html#_build_statements).
It can also discover module dependencies -- albiet crudely.

This example shows how to use `mobius + ninja + clang` to:

 * Modularize most of libc++ -- following the [work done](https://github.com/build2/libstd-modules) by the [build2](https://build2.org/) people.
 * Add a new module of [nonstd::observer_ptr](https://en.cppreference.com/w/cpp/experimental/observer_ptr), the world's "dumbest" smart pointer, not yet in the llvm's libc++.
 * Create a new module `stuff.example`
 * Compile and run a trivial program that uses it all.

The `modules-ts` is essentially undocumented, so this example is what I could figure out on my own.
Feedback is welcome.

### Build and Install Clang Trunk ###

`modules-ts` is unusable in `clang-7.0` and earlier, so you need to build `trunk` from source.
The `build-clang.sh` script does precisely this, installing eveything into `/opt/llvm/trunk`

```
 > cd /path/to/examples/directory
 > ./build-clang.sh
```

### Build and Install Mobius ###

Mobius will be installed to `/usr/local/bin`

```
 > cd /path/to/mobiud/directory
 > make
 > sudo make install
```

The instructions are available at the command prompt.

```
 > mobius -h
```

### Build and Run the Example ###

```
 > ./run.sh $USER
```

The run manages several different build and run configurations.
The available options are listed at the top of `run.sh`.

### About ###

This is a _simple_ and _extensible_ build system.
I've written my own advanced `Makefiles`, and tried various other build
systems.
This is my system of choice.
I do use a custom build of `ninja` that improves the output.

