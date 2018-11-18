
# mobius
A preprocessor for [ninja build](https://ninja-build.org/)

Ninja is not designed to be "convenient to edit by hand", and the developes strongly suggest using some form of machine generation. Well, here it is. Mobius is a preprocessor for `ninja.build` files, and should be suitable for most small, or even mid-sized projects. It takes about 1 millisecond to run on a 20000 line work project with a series of complex build rules that includes `moc`, `qrc`, `boost-python`, and `protoc`, with the option of using either `clang` and `gcc`, and five build modes. (`debug`, `release`, and three sanitizers: `usan`, `asan`, and `tsan`.)  

## Requirements

 * Linux
 * C++17
 
 Should be simple to port to other platforms; there's only one source file. 

## Installation

Install clang-5.0, or gcc-7, or newer.

```
make
sudo make install
```

## Instructions

```
mobius -h
```

## Examples

So far there's only one example: [clang + modules-ts](https://github.com/aaron-michaux/mobius/tree/master/examples/clang-modules-ts). 


