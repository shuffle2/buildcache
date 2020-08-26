# Using BuildCache

To use BuildCache for your builds, simply prefix the build command with
`buildcache`. For instance:

```bash
$ buildcache g++ -c -O2 hello.cpp -o hello.o
```

## Using with CMake

A convenient solution for bigger CMake-based projects is to use the
`RULE_LAUNCH_COMPILE` property to use BuildCache for all compilation commands,
like so:

```cmake
find_program(buildcache_program buildcache)
if(buildcache_program)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${buildcache_program}")
endif()
```

## Symbolic links

Another alternative is to create symbolic links that redirect invokations of
your favourite compiler to go via BuildCache instead. For instance, if
`$HOME/bin` is early in your PATH, you can do the following:

```bash
$ ln -s /path/to/buildcache $HOME/bin/cc
$ ln -s /path/to/buildcache $HOME/bin/c++
$ ln -s /path/to/buildcache $HOME/bin/gcc
$ ln -s /path/to/buildcache $HOME/bin/g++
…
```

You can check that it works by invoking the compiler with BuildCache debugging
enabled:

```bash
$ BUILDCACHE_DEBUG=1 gcc
BuildCache[52286] (DEBUG) Invoked as symlink: gcc
…
```

## Impersonating a wrapped tool

Setting `BUILDACHE_IMPERSONATE` forces BuildCache to operate as a tool wrapper,
using the value of the property as the tool to wrap. This allows pointing build
systems directly at the BuildCache executable instead of using symbolic links.
Note that when this setting has a non-default value BuildCache command line
arguments cannot be used - since any arguments are always forwarded to the
wrapped tool.

For example:

```bash
# Wraps execution of "g++ -c -O2 hello.cpp -o hello.o"
$ BUILDCACHE_IMPERSONATE=g++ buildcache -c -O2 hello.cpp -o hello.o

# Wraps execution of "g++ -s", probably not desired!
$ export BUILDACHE_IMPERSONATE=g++
$ buildcache -s
```

## Using with icecream

[icecream](https://github.com/icecc/icecream) (or ICECC) is a tool for
distributed compilation. To use icecream you can set the environment variable
`BUILDCACHE_PREFIX` to the icecc executable, e.g:

```bash
$ BUILDCACHE_PREFIX=/usr/bin/icecc buildcache g++ -c -O2 hello.cpp -o hello.o
```

## Using a shared remote cache

To improve the cache hit ratio in a cluster of machines that often perform
the same or similar build tasks, you can use a shared remote cache (in
addition to the local cache).

To do so, set `BUILDCACHE_REMOTE` to a valid remote server address (see below).

### Redis

[Redis](https://redis.io/) is a fast, in-memory data store with built in
[LRU](https://en.wikipedia.org/wiki/Cache_replacement_policies#Least_recently_used_(LRU))
eviction policies. It is suitable for build systems that produce many small
object files, such as is typical for C/C++ compilation.

Example:
```bash
$ BUILDCACHE_REMOTE=redis://my-redis-server:6379 buildcache g++ -c -O2 hello.cpp -o hello.o
```

### S3

[S3](https://en.wikipedia.org/wiki/Amazon_S3) is an open HTTP based protocol
that is often provided by [object storage](https://en.wikipedia.org/wiki/Object_storage)
solutions. [Amazon AWS](https://aws.amazon.com/) is one such service. An open
source alternative is [MinIO](https://min.io/).

Compared to a Redis cache, an S3 object store usually has a higher capacity and
a slightly higher performance overhead. Thus it is better suited for larger
build artifacts.

When using an S3 remote, you also need to define `BUILDCACHE_S3_ACCESS` and
`BUILDCACHE_S3_SECRET`. You will also need to create a bucket for BuildCache
in your S3 storage, and configure some retention policy (e.g. periodic LRU
eviction).

Example:
```bash
$ BUILDCACHE_REMOTE=s3://my-minio-server:9000/my-buildcache-bucket BUILDCACHE_S3_ACCESS="ABCDEFGHIJKL01234567" BUILDCACHE_S3_SECRET="sOMloNgSecretKeyThatsh0uldnotBeshownatAll" buildcache g++ -c -O2 hello.cpp -o hello.o
```

## Using with Visual Studio / MSBuild

* Create a symlink named `cl.exe` pointing to your `buildcache.exe`.
  * Alternatively, set `BUILDCACHE_IMPERSONATE` to `cl.exe`.

Additionally, several default project settings have to be changed:

* Change object file names from `$(IntDir)` to `$(IntDir)%(Filename).obj` to get one compiler invocation per source file.
  * Can be set by opening a project's properties, then *Configuration Properties*, *C/C++*, *Output Files* page, *Object File Name* setting,
  * Alternatively define the `<ObjectFileName>` property inside the `<ClCompile>` ItemDefinitionGroup in your `vcxproj` file.
* Since the previous step turns off compiler level parallelism, restore performance using [`MultiToolTask`](https://devblogs.microsoft.com/cppblog/improved-parallelism-in-msbuild/).
  * Can be turned on using the `<UseMultiToolTask>` property inside the `"Globals"` PropertyGroup in your `vcxproj`.
* Set `<CLToolExe>` property to the symlink created previously.
  * Also placed inside the `"Globals"` PropertyGroup in your `vcxproj`.
