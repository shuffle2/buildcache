# Configuration

BuildCache can be configured via environment variables and a per-cache JSON
configuration file. The optional configuration file is located in the cache
root directory, and is called `config.json` (e.g.
`$HOME/.buildcache/config.json`).

The following options control the behavior of BuildCache:

| Env | JSON | Description | Default |
| --- | --- | --- | --- |
| `BUILDCACHE_DIR` | - | The cache root directory | `$HOME/.buildcache` |
| `BUILDCACHE_PREFIX` | `prefix` | Prefix command for cache misses | None |
| `BUILDCACHE_REMOTE` | `remote` | Address of remote cache server (`protocol://host:port/path`, where `protocol` can be `redis` or `s3`, and `port` and `path` are optional) | None |
| `BUILDCACHE_S3_ACCESS` | `s3_access` | S3 access key | None |
| `BUILDCACHE_S3_SECRET` | `s3_secret` | S3 secret key | None |
| `BUILDCACHE_LUA_PATH` | `lua_paths` | Extra path(s) to Lua wrappers | None |
| `BUILDCACHE_DEBUG` | `debug` | Debug level | None |
| `BUILDCACHE_LOG_FILE` | `log_file` | Log file path (empty for stdout) | None |
| `BUILDCACHE_MAX_CACHE_SIZE` | `max_cache_size` | Cache size limit in bytes | 5368709120 |
| `BUILDCACHE_MAX_LOCAL_ENTRY_SIZE` | `max_local_entry_size` | Local cache entry size limit in bytes (uncompressed) | 134217728 |
| `BUILDCACHE_MAX_REMOTE_ENTRY_SIZE` | `max_remote_entry_size` | Remote cache entry size limit in bytes (uncompressed) | 134217728 |
| `BUILDCACHE_HARD_LINKS` | `hard_links` | Allow the use of hard links when caching | false |
| `BUILDCACHE_CACHE_LINK_COMMANDS` | `cache_link_commands` | Enable caching of link commands | false |
| `BUILDCACHE_COMPRESS` | `compress` | Allow the use of compression when caching (overrides hard links) | false |
| `BUILDCACHE_COMPRESS_FORMAT` | `compress_format` | Cache compresion format (see below) | DEFAULT |
| `BUILDCACHE_COMPRESS_LEVEL` | `compress_level` | Cache compresion level (see below) | -1 |
| `BUILDCACHE_PERF` | `perf` | Enable performance logging | false |
| `BUILDCACHE_DISABLE` | `disable` | Disable caching (bypass BuildCache) | false |
| `BUILDCACHE_ACCURACY` | `accuracy` | Caching accuracy (see below) | DEFAULT |
| `BUILDCACHE_READ_ONLY` | `read_only` | Only read and use the cache without updating it | false |
| `BUILDCACHE_IMPERSONATE` | `impersonate` | Explicitly set the executable to wrap | None |
| `BUILDCACHE_LOCAL_LOCKS` | `local_locks` | Use faster lockfile implementation, only safe if the cache is not on a fileshare | false |

Note: Currently, only the TI C6x back end supports the `cache_link_commands`
option.

An example configuration file:

```json
{
  "max_cache_size": 10000000000,
  "prefix": "icecc",
  "remote": "redis://my-server:6379",
  "debug": 3,
  "lua_paths": [
    "/home/myname/buildcache-lua",
    "/opt/buildcache-lua"
  ],
  "compress": true
}
```

To see the configuration options that are in effect, run:

```bash
$ buildcache -s
```

## Debugging

To get debug output from a BuildCache run, set the environment variable
`BUILDCACHE_DEBUG` to the desired debug level (debug output is disabled by
default):

| BUILDCACHE_DEBUG | Level | Comment              |
| ---------------- | ----- | -------------------- |
| 1                | DEBUG | Maximum printouts    |
| 2                | INFO  |                      |
| 3                | ERROR |                      |
| 4                | FATAL |                      |
| -1               | -     | Disable debug output |

For instance:

```bash
$ BUILDCACHE_DEBUG=2 buildcache g++ -c -O2 hello.cpp -o hello.o
```

It is also possible to redirect the log output to a file using the
`BUILDCACHE_LOG_FILE` setting.

## Caching accuracy

With the caching accuracy setting, `BUILDCACHE_ACCURACY`, it is possible to
control how strict BuildCache is when checking for cache hits. This gives an
opportunity to trade correctness for performance.

| BUILDCACHE_ACCURACY | Comment                                       |
| ------------------- | --------------------------------------------- |
| STRICT              | Maximum correctness                           |
| DEFAULT             | A balance between performance and correctness |
| SLOPPY              | Optimize for maximum cache hit ratio          |

The default accuracy mode is `DEFAULT`.

### STRICT

In `STRICT` accuracy mode, the cache lookup will consider absolute file paths
and line numbers whenever debugging symbols or coverage info is generated. This
means that when your build includes debugging symbols or coverage info, you will
get a cache miss if the absolute file path or any line number has changed.

This mode is suitable if you intend to use the final executable for running code
coverage tests or for debugging. The downside is that you may often get cache
misses, especially in a shared centralized cache that contains objects from
different machines with different build paths.

### DEFAULT

The `DEFAULT` mode is similar to the `STRICT` mode, except that it will ignore
file path and line number information for debug builds.

Note that in many situations it is still possible to use the generated
executables for debugging. For instance, with GDB you can
[specify a custom source code path](https://sourceware.org/gdb/current/onlinedocs/gdb/Source-Path.html)
during a debugging session.

Binaries built with this mode can be used for code coverage generation.

### SLOPPY

With the `SLOPPY` mode, absolute file paths and line number information are
always ignored during cache lookup, which improves cache hit ratio. The downside
is that you may not be able to use the binaries for code coverage.

## Cache compression format

With the cache compression format setting, `BUILDCACHE_COMPRESS_FORMAT`, it is
possible to control how the generated caches are compressed.

Note: The "compress" setting must be set to true in order to utilize this
setting.

| BUILDCACHE_COMPRESS_FORMAT   | Comment                                                            |
| ---------------------------- | ------------------------------------------------------------------ |
| LZ4                          | Utilize LZ4 compression (faster compression, larger cache sizes)   |
| ZSTD                         | Utilize ZSTD compression (slower compression, smaller cache sizes) |
| DEFAULT                      | Utilize LZ4 compresson                                             |

The default compression format is `DEFAULT`.

## Cache compression level

With the cache compression level setting, `BUILDCACHE_COMPRESS_LEVEL`, it is
possible to control the effort exerted by the compressor in order to produce
smaller cache files. See the documentation of your chosen compressor for more
information.

The default compression level is -1, which will utilize the default compression
level for the compressor.

Note: The "compress" setting must be set to true in order to utilize this
setting.
