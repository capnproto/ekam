# Ekam VS Code Plugin

Brings error reports from Ekam into Visual Studio Code.

## Usage

The extension will look for `ekam-langserve` in your `PATH`, or you can specify
its location in your config like:

```json
{
    "ekam.path": "/absolute/path/to/ekam-langserve",
    "ekam.args": [ "localhost:41315" ]
}
```

To obtain `ekam-langserve` binary, build [Ekam](https://github.com/capnproto/ekam).
The binary will end up in `bin/ekam-langserve` after the build completes.

The language server expects to connect to a local Ekam run. You'll need to tell
Ekam to publish logs on a local port by running it like:

    ekam -c -n :41315

## Building from source

```bash
npm install
npm run postinstall
npm run package
```

This builds `vscode-ekam.vsix`, which you can then install into VS Code.
