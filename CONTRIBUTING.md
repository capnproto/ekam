## Contributing to Ekam

### Building

Use `make continuous` to build Ekam continuously and watch for changes. For LSP support in Visual Studio Code, run `make setup-vscode`.

`make continuous` requires you already have an install of Ekam. You can install Ekam by building it once (`make`) and then installing `bin/ekam{,-client,-langserve}` to a directory in PATH.

Note that `g++` tends to generate spurious warnings, so you may want to use `make continuous CXX=clang++` instead.

### Overview

Ekam has multiple components.
- `ekam`: This is the main binary. It actually builds the code, and uses capnp to communicate with the other binaries.
- `ekam-client`: This is a small tool to view the build output from `ekam`.
- `ekam-langserve`: This is a Language Server Protocol client that communicates with `ekam`.
- `ekam-bootstrap`: This is the same as `ekam`, but doesn't support the remote capnp protocol. As a result, some flags (like `-n`) aren't supported, and prebuilt capnp clients won't be able to connect.

`ekam-bootstrap` is built directly by `make`. All other components are built by `ekam-bootstrap`.
