# CLeonOS pkg

`pkg` is the CLeonOS package installer for user-space ELF applications.

## Client Commands

```sh
pkg install <name>
pkg install --dry-run <name>
pkg install --reinstall <name>
pkg reinstall <name>
pkg install /path/to/app.elf
pkg install /path/to/app.clpkg
pkg install http://host/path/app.clpkg
pkg install http://host/path/app.elf
pkg list
pkg info <name>
pkg files <name>
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
pkg doctor
pkg verify [name]
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

Safety and diagnostics:

```sh
pkg install --dry-run hello
pkg install --reinstall hello
pkg files hello
pkg doctor
pkg verify
pkg verify hello
```

`pkg install --dry-run` resolves dependencies, target paths, download sizes when
the repository provides them, and whether an install would overwrite an existing
ELF. It does not write `/shell` or `/system/pkg/installed.db`.

`pkg install --reinstall <name>` and `pkg reinstall <name>` force a fresh
install of an already installed package. Normal `pkg install <name>` refuses to
overwrite an installed package, so accidental reinstalls do not hide local
damage or version drift.

`pkg files <name>` lists files recorded for the package. Current packages record
the primary ELF target, typically `/shell/<name>.elf`; the command is structured
so future multi-file packages can add more entries without changing the user
interface.

Write operations use `/system/pkg/lock` to avoid concurrent installers changing
`installed.db` or `/shell/*.elf` at the same time. Stale locks owned by exited
processes are ignored automatically.

`pkg doctor` checks the network stack, repository API, `/system/pkg` and
`/shell` write probes, `installed.db` parsing, and disk presence/mount status.
The current kernel does not expose free-space accounting yet, so doctor reports
disk capacity and uses write probes instead of an exact free-byte check.

`pkg verify [name]` checks `installed.db`, verifies that installed ELF targets
still exist, and compares SHA-256 when a checksum was recorded. Older
installations without a recorded checksum report a warning instead of failing.

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
