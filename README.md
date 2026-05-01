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
pkg remote list
pkg remote info <name>
pkg search <keyword>
pkg category <name>
pkg tag <name>
pkg update
pkg upgrade <name>
pkg upgrade --all
pkg repo
pkg repo http://10.0.2.2/pkg/index.php
```

Installed applications are copied to `/shell/<name>.elf` by default and are
tracked in `/system/pkg/installed.db`.

Remote commands use the repository configured by `pkg repo`:

```sh
pkg remote list
pkg remote info hello
pkg search hello
pkg category network
pkg tag gui
```

Update commands compare local installed versions against the remote repository:

```sh
pkg update
pkg upgrade hello
pkg upgrade --all
```

## Manifest Format

`*.clpkg` is a plain text manifest:

```ini
format=cleonos-pkg-v1
name=hello
version=1.0.0
target=/shell/hello.elf
url=http://10.0.2.2/pkg/index.php?download=hello
description=Hello from the CLeonOS kit.
depends=libfoo>=1.0.0,libbar
category=demo
tags=hello,sample
```

Local manifests can use a local ELF path instead:

```ini
format=cleonos-pkg-v1
name=hello
version=1.0.0
target=/shell/hello.elf
elf=hello.elf
depends=libfoo>=1.0.0,libbar
category=demo
tags=hello,sample
```

`target` is restricted to `/shell/*.elf`.
`depends` is a comma-separated dependency list. Each entry supports `name`,
`name@version`, `name=version`, `name>=version`, `name<=version`,
`name>version`, `name<version`, and `name!=version`.

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

### Repository API

The server keeps the legacy manifest/download routes and also exposes JSON API
routes:

```text
GET  /index.php?api=list
GET  /index.php?api=info&name=hello
GET  /index.php?api=search&q=hello
GET  /index.php?api=category&name=network
GET  /index.php?api=tag&name=gui
GET  /index.php?api=me
POST /index.php?api=register
POST /index.php?api=login
GET  /index.php?api=logout
POST /index.php?api=upload
POST /index.php?api=update
```

`api=list` returns all packages. `api=info` returns one package by name.
`api=search` matches package name, description, version, owner, dependencies,
category and tags.
`api=category` and `api=tag` filter packages by exact category or tag.

Auth API fields:

```text
username=<name>
password=<password>
```

Upload API uses `multipart/form-data` and requires login:

```text
name=<package-name>
version=<version>
target=/shell/<package-name>.elf
description=<short text>
depends=<dependency constraints>
category=<category>
tags=<comma-separated tags>
elf=<uploaded ELF file>
```

Update API also uses `multipart/form-data` and requires login as the package
owner:

```text
name=<existing-package-name>
version=<version>
target=/shell/<package-name>.elf
description=<short text>
depends=<dependency constraints>
category=<category>
tags=<comma-separated tags>
elf=<optional replacement ELF file>
```

Only the package creator can update an existing package. Metadata can be
changed without uploading a new ELF. If `elf` is present, the stored package
binary is replaced after ELF magic validation.

The uploaded file is normalized to:

```text
server/packages/<name>/<name>.elf
server/packages/<name>/package.ini
```

User accounts are stored in `data/users.json`, outside the `server/` document
root. The `data/` directory is ignored by git.

Server package layout:

```text
server/packages/hello/hello.elf
server/packages/hello/package.ini
```

The web page intentionally uses plain HTML only, with no CSS and no JavaScript.

## License

Apache License 2.0. See `LICENSE`.
