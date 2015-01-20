# Ekam Build System

Ekam ("make" backwards) is a build system which automatically figures out what to build and how to build it purely based on the source code. No separate "makefile" is needed.

Ekam works by exploration. For example, when it encounters a file ending in ".cpp", it tries to compile the file. If there are missing includes, Ekam continues to explore until it finds headers matching them. When Ekam builds an object file and discovers that it contains a "main" symbol, it tries to link it, searching for other object files to satisfy all symbol references therein.

Thus Ekam is, in fact, Make in reverse. Make starts by reading a Makefile, sees what executables it wants to build, then from there figures out what source files need to be compiled to link into them, then compiles them. Ekam starts by looking for source files to compile, then determines what executables it can build from them, and, in the end, might output a Makefile describing what it did.

Ekam is a work in progress.

## Building Ekam

### Warning

Ekam is an experimental project that is not ready for wide use.  These instructions are frequently out-of-date, and sometimes the code in the repository doesn't even build.  The bootstrap script in particular tends not to get updated when it should.  You may need to go back a few revisions to make things work, or do some debugging, or complain to me about it.  :)

### Supported Platforms

At this time, Ekam only runs on Linux.  It requires GCC 4.8+ or Clang 3.3+, as it uses C++11 features.

In the past, Ekam worked on FreeBSD and Max OSX, but the code to support that atrophied and was eventually deleted. Ekam uses a lot of OS-specific hacks and so is unlikely to work on any platform which is not explicitly supported.

We'd like to see other platforms supported, but Ekam's primary user and maintainer right now (Sandstorm.io) is itself highly Linux-specific, so there is not much pressure. (Let us know if you want to help.)

### Bootstrapping the build

Download and compile Ekam like so:

    hg clone https://ekam.googlecode.com/hg/ ekam
    cd ekam
    ./bootstrap.sh

If successful, Ekam should have built itself, with the output binary at "bin/ekam".

### You will see errors

When Ekam builds itself, some errors will be produced, but the build will be successful overall.  Pay attention to the final message printed by the bootstrap script to determine whether there was a real problem.

For example, if Cap'n Proto is not available (see below), you will see errors about `ProtoDashboard.cpp`, `dashboard.capnp`, and `ekam-client.cpp`. Building these files are optional (they enable the network protocol); Ekam will still work without them.

### Compiling Ekam with Ekam

Compiling Ekam requires the GCC flag -std=gnu++0x to enable C++11 features, but currently there is no way for the code itself to specify compiler flags that it requires.  You can only specify them via environment variable.  So, to build Ekam with Ekam, type this command at the top of the Ekam repository:

    CXXFLAGS=-std=gnu++0x ekam -j4

The `-j4` tells Ekam to run up to four tasks at once.  You may want to adjust this number depending on how many CPU cores you have.

Note that Ekam looks for a directory called `src` within the current directory, and scans it for source code.  The Ekam source repository is already set up with such a `src` subdirectory containing the Ekam code.  You could, however, place the entire Ekam repository _inside_ some other directory called `src`, and then run Ekam from the directory above that, and it will still find the code.  The Protocol Buffers instructions below will take advantage of this to create a directory tree containing both Ekam and protobufs.

Ekam places its output in siblings of `src` called `tmp` (for intermediate files), `bin` (for output binaries), `lib` (for output libraries), etc.  These are intended to model Unix directory tree conventions.

## Continuous Building

If you invoke Ekam with the `-c` option, it will watch the source tree for changes and rebuild derived files as needed.  In this way, you can simply leave Ekam running while you work on your code, and get information about errors almost immediately on saving.

Note that this feature works best on Linux.  FreeBSD and OSX lack a scalable way to watch large directory trees.  Ekam will use `kqueue`'s `EVFILT_VNODE` on these systems, but this can be problematic because it requires opening a file descriptor for every file watched.  Particularly on OSX, this can quickly exhaust the per-process file descriptor limit.

## IDE plugins and other external clients

Ekam can, while running, export a network interface which allows other programs to query the state of the build, including receiving the task tree and error logs. To support this, you must compile Ekam with Cap'n Proto.

### Compiling with Cap'n Proto

If you clone the [Cap'n Proto](https://capnproto.org) git repository next to Ekam's, i.e. so that it is found at `../capnproto`, then Ekam will be built with Cap'n Proto. Note that you do not have to build or install Cap'n Proto separately; Cap'n Proto's sources are merged into Ekam's build and built by Ekam. (If you look in the `src` directory, you'll see this is accomplished through some sneaky symlinks.)

### Ekam Client

If you compile Ekam with Cap'n Proto, then Ekam's `ProtoDashboard.cpp` and `ekam-client` should compile correctly.  This enables the `-n` flag to Ekam, which tells it to allow clients to query its state over the network.  Invoke like:

  ekam -n :41315

Then attach the client like so:

  nc localhost 41315 | ekam-client

(A future version of the client will be able to connect directly rather than use netcat in this way.)

### Qt Creator Plugin

The `qtcreator` directory in the Ekam repository contains source code for a [Qt Creator](http://qt-project.org/wiki/Category:Tools::QtCreator) plugin. Qt Creator is an open source C++ IDE that, despite its name, is quite nice even for non-Qt code.

To build the Qt Creator plugin, you'll want to download the Qt Creator source code release and build it from scratch. Then, go into the `qtcreator` directory in the Ekam repo and do the following:

    export QTC_SOURCE=<path to qt creator source directory>
    qmake
    make

This should build the plugin and install it directly into your Qt Creator build.

Now you can start Qt Creator and choose the "Ekam Actions" view in the navigation frame. The plugin connects to a local Ekam instance running on port 41315; so, start ekam like this:

  ekam -c -n :41315

The plugin will also add error markers to you source code, visible in the issue tracker. Double-clicking on a failed rule in the tree view will navigate the issues view to the first message from that action, which you can in turn use to navigate to the source location of the error.

## Using Ekam in your own code

### Preferred project layout

An Ekam project directory must contain a directory called `src` which contains all source code. Ekam will ignore everything other than this `src` directory.

Ekam will create sibling directories called `tmp`, `bin`, and `lib`; you probably shouldn't have any files in those directories to start, because it's nice to be able to `rm -rf` them to clean.

### Import the rule files

In order for Ekam to be able to build a project, it needs to find its rule files, which are under `src/ekam/rules` in Ekam's own source code. You will want to symlink this directory into your project's source tree somewhere. It does not have to be anywhere in particular, but `src/ekam-rules` is probably a good bet.

### Compiler flags

You can set the following environment variables to control how Ekam compiles your code:

* `CXX`: Sets the C++ compiler, e.g. `CXX=clang++`.
* `CXXFLAGS`: Sets C++ compilation flags, e.g. `CXXFLAGS=-std=c++11 -O2 -Wall`.
* `LIBS`: Sets linker flags, e.g. `LIBS=-lsodium -lz`

### Building binaries

When Ekam finds or compiles a `.o` file that defines the symbol `main`, it will attempt to link the file with other `.o` files defining all needed symbols (transitively) in order to produce a binary. The binary takes the name of the `.o` with the extension removed, e.g. `foo.o` produces a binary `foo`.

The binary is initially just dropped into the `tmp` directory, which mirrors the `src` directory. For example, `src/foo/bar.c++` will be compiled to `src/foo/bar.o`, which will link (if it defines `main`) into `src/foo/bar`. If you'd like for the binary to be output to the `bin` directory, you must declare a manifest file with the extension `.ekam-manifest`. This file is a simple text file where each line names a file and its output directory. For example:

    bar bin

This says: "Once you've built `bar` (within this directory), copy it into `bin`."

You can also choose to rename the file when installing:

    bar bin/baz

This says: "Once you've built `bar` (within this directory), copy it to `bin/baz`."

### Debugging

If you want `gdb` to be able to find your code when debugging Ekam-built binaries, you should set up a couple symlinks as follows:

    mkdir ekam-provider
    ln -s ../src ekam-provider/canonical
    ln -s ../src ekam-provider/c++header

To understand the reason for these symlinks, see the explanation of `intercept.so` later in this document.

### Tests

If Ekam builds a binary which ends in `-test` or `_test`, it will run that binary and report pass/fail depending on the exit status. Passing tests are marked in green in the output (whereas regular successful build actions are blue) in order to help you develop a pavlovian attachment to writing tests and seeing them pass.

Ekam has additional built-in support for two test frameworks: Google Test and KJ tests. When Ekam compiles a source file which declares test cases using one of these frameworks (e.g. using Google Test's `TEST` or `TEST_F` macros, or KJ's `KJ_TEST` macro) but without a `main` function, it will automatically link against the respective framework's test runner (which supplies `main`) in order to produce a test binary. Note that the detection of test declarations is based on linker symbols, not on scanning the source code, so don't worry if you've declared your own wrapper macros.

In order for Google Test or KJ test integration to work, the respective test framework's code must be in your source tree as a dependency (see below).

### Dependencies

If your project depends on other projects, and you want to build those other projects as part of your own build (rather than require the user to install the libraries on their system), you should follow the pattern that Ekam itself does: tell the user to download the dependencies into sibling directories of your own repo, and then use symlinks to pull their source directories into your own.

For example, to symlink Cap'n Proto into your project:

    ln -s ../../capnproto/c++/src/{kj,capnp} src

We may develop tools to automate this in the future.

### Non-Ekam-clean Dependencies

If your dependency is not organized for Ekam, you might need to link to its top-level directory rather than a source subdirectory. For instance, Google Test separates its source code into `src` and `include` directories, so if you only link in its `src` you'll miss the headers. Instead, link in the whole repo like:

    ln -s ../../gtest src

When Ekam sees a directory named `include`, it adds that directory to the include path for all other code it compiles. Thus the Google Test headers will end up includable by your code.

Note that projects imported this way will usually have a bunch of errors reported since they are not Ekam-clean. However, often (such as in the case of Google Test) the important parts will compile well enough to use.

## Custom Rules

You may teach Ekam how to handle a new type of file -- or introduce a one-off build rule not triggered by any file -- by creating a rule file. A rule file is any executable with the extension `.ekam-rule`. Often, they are shell scripts, but this is not a requirement. Rule files can themselves be the output of other rules, so you could compile a C++ program that acts as a rule.

Ekam executes custom rules and then communicates with them on stdin/stdout using a simple line-based text protocol described below.

When Ekam encounters an executable with the `.ekam-rule` extension, it first runs it with no arguments in order to "learn" it. At that time, the program may tell ekam what kinds of files it should trigger on using the `trigger` command (below). When Ekam encounters a trigger file, it will execute the rule again with the trigger file's canonical name as the argument. Alternatively, if you just want to implement a one-off action, then the rule may simply perform that action at "learn" time without registering any triggers.

### Canonical file names

A file's "canonical" name is the name without the `src/` or `tmp/` prefix. Thus canonical names do not distinguish between source files and build outputs. No two files can have the same canonical name -- it is an error for a build action to output a file which exists in the `src` directory, or for two actions to produce the same file under `tmp`.

You can ask Ekam to map a canonical name to a physical disk name using the `findInput` command.

### Tags

Ekam rules are triggered by tags. A tag is a simple string, conventionally of the format `type:value`. Tags are applied to files. For example, a `.o` file declaring the symbol `main` might be given the tag `c++symbol:main`. Rules may assign new tags to any file (source or output).

Ekam has some default rules that assign tags meant for broad use:

* `canonical:<filename>`: Each file receives this tag, where `<filename>` is its canonical name.
* `filetype:<extension>`: Each regular file that has a file type extension receives this tag, e.g. `filetype:.c++`.
* `directory:*`: Each directory receives this tag.

### Commands

A rule may perform the following commands by writing them to standard output.

* `trigger <tag>`: Used during the learning phase to tell Ekam that the rule should be executed on any file tagged with `<tag>`.
* `verb <text>`: Use during the learning phase to tell Ekam the rule's "verb", which is what is displayed to the user when the rule later runs. This should be a simple, descriptive word. For instance, for a C++ compile action, the verb is `compile`.
* `silent`: Use during the learning phase to indicate that when this command later runs, it should not be reported to the user unless it fails. Use this to reduce noise caused by very simple commands that perform trivial actions.
* `findInput <file>`: Obtains the canonical name of the given file. Ekam will reply by writing one line to the rule's standard input containing the full disk path of the file (e.g. including `src/` or `tmp/`). Ekam will remember that the build action depended on this file, so if the file changes, the action will be re-run.
* `findProvider <tag>`: Find a file tagged with `<tag>`. If there are multiple matches, Ekam heuristically chooses the "preferred" one, which generally means the one closest in the directory tree to the file which triggered the rule. The path is returned as with `findInput`. Also as with `findInput`, the file is considered a dependency of the action. Ekam will re-run this action if the file changes *or* if the file Ekam chose to match `<tag>` changes.
* `noteInput <external-file>`: Tells Ekam that the action depends on `<external-file>`, which is a path outside of the project's source tree. For instance, `/usr/include/stdlib.h`. Currently Ekam ignores this, but in theory it could watch these files and re-run the action if they change.
* `newOutput <canonical-name>`: Create a new output file with the given canonical name. Ekam replies by writing the on-disk path where the file should be created to the rule's standard input.
* `provide <filename> <tag>`: Tag `<filename>` (a canonical name) with `<tag>`. The file must be a known input our output of this rule; i.e. it must have been the subeject of a previous call to `findInput`, `findProvider`, or `newOutput`.
* `install <filename> <location>`: Take the canonical filename `<filename>` and copy it to `<location>`, where `<location>` should start with `bin/`, `lib/`, etc.
* `passed`: Indicate that this action ran a test, and the test passed.

### `intercept.so`

Sometimes, it's hard to know what a build tool's exact inputs and outputs will be ahead of time. For instance, a C++ compiler run will need to input all of the header files `#include`ed by the source file. There's no reasonable way to know what these might be in advance, much less look up the locations of files to satisfy each.

To solve this, Ekam implements a library which can be injected into a tool via `LD_PRELOAD` in order to intercept filesystem calls, automatically issues the proper Ekam commands to register inputs and outputs, and then map the call to the real disk path.

The interceptor library is implemented in `src/ekam/rules/intercept.c` and built by `src/ekam/rules/intercept.ekam-rule`. You may request it by requesting the tag `special:ekam-interceptor`, which maps to the compiled `intercept.so`, which you may then inject into an arbitrary command using `LD_PRELOAD`. See `src/ekam/rules/compile.ekam-rule` to see how this is done with the C++ compiler.

When filesystem calls are being intercepted, an attempt to open a regular filename will be heuristically mapped to the closest file whose canonical name contains the full requested name as a suffix. For example, when processing `src/foo/bar/baz`, if the tool tries to open `qux/corge`, this could map to `src/foo/bar/qux/corge` or `src/qux/corge` or `src/grault/qux/corge`, but **not** `src/grault/corge` nor `src/qux/corge/grault` nor `src/corge`.

If you want to open a file by tag, the special virtual path `/ekam-provider/<tag-type>/<tag-value>` can be used. For example, since the `include.ekam-rule` rule tags all C++ header files with `c++header:<include-path>`, when we invoke the compiler using the inteceptor, we pass the flag `-I/ekam-provider/c++header`.

If you want to open a file purely by its whole canonical path (not using the heuristic that finds nearby files), you may do so by opening `/ekam-provider/canonical/<canonical-name>`, since as described above every file gets tagged with `canonical:<canonical-name>`.
