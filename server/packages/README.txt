Package layout:

packages/
  hello/
    hello.elf
    package.ini

package.ini fields:

version=1.0.0
target=/shell/hello.elf
description=Hello world package built by the CLeonOS kit.

Only the ELF file is required. If package.ini is absent, version defaults to
1.0.0 and target defaults to /shell/<name>.elf.

Default sample:

Run this command from the server directory:

bdt sample

It builds sample/apps/hello/main.c with the root kit and generates:

packages/hello/hello.elf
packages/hello/package.ini

The sample build is server-local. It is not part of the root userapps target.
