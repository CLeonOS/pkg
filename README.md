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
pkg remove --force <name>
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
sha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
deprecated=Use hello2 instead.
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
`sha256` is optional for local manifests, but repository manifests include it
automatically. The client verifies the downloaded ELF before installing.
`deprecated` is optional. If present, the client warns before installing.

## Uninstall Protection

Installed package records include dependency metadata for packages installed by
this client version. `pkg remove <name>` refuses to remove a package when another
installed package depends on it:

```sh
pkg remove libfoo
```

Force removal bypasses the reverse-dependency check:

```sh
pkg remove --force libfoo
pkg remove -f libfoo
```

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
POST /index.php?api=deprecate
POST /index.php?api=delete
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

Upload API uses `multipart/form-data` and requires login. The ELF file must be
no larger than 3 MiB:

```text
name=<package-name>
version=<version>
target=/shell/<package-name>.elf
description=<short text>
depends=<dependency constraints>
category=<category>
tags=<comma-separated tags>
deprecated=<optional warning text>
elf=<uploaded ELF file>
```

Update API also uses `multipart/form-data` and requires login as the package
owner. Replacement ELF uploads are also limited to 3 MiB:

```text
name=<existing-package-name>
version=<version>
target=/shell/<package-name>.elf
description=<short text>
depends=<dependency constraints>
category=<category>
tags=<comma-separated tags>
deprecated=<optional warning text>
elf=<optional replacement ELF file>
```

Only the package creator can update an existing package. Metadata can be
changed without uploading a new ELF. If `elf` is present, the stored package
binary is replaced after ELF magic validation.

Only the package creator can deprecate or delete a package:

```text
POST /index.php?api=deprecate
name=<existing-package-name>
deprecated=<warning text, empty to clear>

POST /index.php?api=delete
name=<existing-package-name>
```

Repository manifests and JSON API responses include `sha256`. The checksum is
computed from the stored ELF, so package owners do not submit it manually.
Public-key package signatures are reserved for a later manifest revision.

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
