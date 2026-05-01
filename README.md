# CLeonOS pkg

`pkg` is the CLeonOS package installer for user-space ELF applications.

## Client Commands

```sh
pkg install <name>
pkg install /path/to/app.elf
pkg install /path/to/app.clpkg
pkg install http://host/path/app.clpkg
pkg install http://host/path/app.elf
pkg list
pkg info <name>
pkg remove <name>
pkg repo
pkg repo http://10.0.2.2/pkg/index.php
```

Installed applications are copied to `/shell/<name>.elf` by default and are
tracked in `/system/pkg/installed.db`.

## Manifest Format

`*.clpkg` is a plain text manifest:

```ini
format=cleonos-pkg-v1
name=hello
version=1.0.0
target=/shell/hello.elf
url=http://10.0.2.2/pkg/index.php?download=hello
description=Hello from the CLeonOS kit.
```

Local manifests can use a local ELF path instead:

```ini
format=cleonos-pkg-v1
name=hello
version=1.0.0
target=/shell/hello.elf
elf=hello.elf
```

`target` is restricted to `/shell/*.elf`.

## Server

The PHP repository server lives in `server/index.php`.

Generate the default hello-world package:

```sh
cd cleonos/c/pkg/server
bdt sample
```

This builds only the server-side sample package and writes:

```text
server/packages/hello/hello.elf
server/packages/hello/package.ini
```

It is intentionally outside the root `userapps` target, so it does not become a
built-in CLeonOS ramdisk app.

Run it on the host:

```sh
cd cleonos/c/pkg/server
php -S 0.0.0.0:8000
```

Inside CLeonOS/QEMU:

```sh
pkg repo http://10.0.2.2:8000/index.php
pkg install hello
```

Server package layout:

```text
server/packages/hello/hello.elf
server/packages/hello/package.ini
```

The web page intentionally uses plain HTML only, with no CSS and no JavaScript.

## License

Apache License 2.0. See `LICENSE`.
