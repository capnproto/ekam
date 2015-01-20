# Ekam Build System

Ekam ("make" backwards) is a build system which automatically figures out what to build and how to build it purely based on the source code. No separate "makefile" is needed.

Ekam works by exploration. For example, when it encounters a file ending in ".cpp", it tries to compile the file. If there are missing includes, Ekam continues to explore until it finds headers matching them. When Ekam builds an object file and discovers that it contains a "main" symbol, it tries to link it, searching for other object files to satisfy all symbol references therein.

Thus Ekam is, in fact, Make in reverse. Make starts by reading a Makefile, sees what executables it wants to build, then from there figures out what source files need to be compiled to link into them, then compiles them. Ekam starts by looking for source files to compile, then determines what executables it can build from them, and, in the end, might output a Makefile describing what it did.

Ekam is a work in progress.

## How to download and run Ekam.

### Warning

Ekam is an experimental project that is not ready for wide use.  These instructions are frequently out-of-date, and sometimes the code in the repository doesn't even build.  The bootstrap script in particular tends not to get updated when it should.  You may need to go back a few revisions to make things work, or do some debugging, or complain to me about it.  :)

### Supported Platforms

At this time, Ekam is known to work on Linux.  It requires GCC version 4.8 or later, as it uses C++11 features.  In the past, Ekam worked on FreeBSD and Max OSX, but the code to support that has atrophied and no longer works.  Ekam uses a lot of OS-specific hacks and so is unlikely to work on any platform which is not explicitly supported.

### Building

Download and compile Ekam like so:

    hg clone https://ekam.googlecode.com/hg/ ekam
    cd ekam
    ./bootstrap.sh

If successful, Ekam should have built itself, with the output binary at "bin/ekam".

#### You will see errors

When Ekam builds itself, some errors will be produced, but the build will be successful overall.  Pay attention to the final message printed by the bootstrap script to determine whether there was a real problem.

The reason for the errors:  Some files are platform-specific (to Linux, FreeBSD, or OSX).  The ones for the platform you aren't using will fail to compile, producing errors.  Ekam will figure out which files *did* compile successfully and use those.  For example, `KqueueEventManager` works on FreeBSD and OSX but not Linux.  `EpollEventManager` works on Linux but not any other platform.  But Ekam only needs one `EventManager` implementation to function, so it will use whichever one compiled successfully.

Additionally, you will see errors about `ProtoDashboard.cpp`, `dashboard.proto`, and `ekam-client.cpp`.  These files can only be compiled if Protocol Buffers is available in the source tree.  But, these implement an optional feature; Ekam can be built without that feature.  See below for more.

#### Compiling Ekam with Ekam

Compiling Ekam requires the GCC flag -std=gnu++0x to enable C++11 features, but currently there is no way for the code itself to specify compiler flags that it requires.  You can only specify them via environment variable.  So, to build Ekam with Ekam, type this command at the top of the Ekam repository:

    CXXFLAGS=-std=gnu++0x ekam -j4

The `-j4` tells Ekam to run up to four tasks at once.  You may want to adjust this number depending on how many CPU cores you have.

Note that Ekam looks for a directory called "src" within the current directory, and scans it for source code.  The Ekam source repository is already set up with such a "src" subdirectory containing the Ekam code.  You could, however, place the entire Ekam repository _inside_ some other directory called "src", and then run Ekam from the directory above that, and it will still find the code.  The Protocol Buffers instructions below will take advantage of this to create a directory tree containing both Ekam and protobufs.

Ekam places its output in siblings of "src" called "tmp" (for intermediate files), "bin" (for output binaries), "lib" (for output libraries), etc.  These are intended to model Unix directory tree conventions.

### Continuous Building

If you invoke Ekam with the `-c` option, it will watch the source tree for changes and rebuild derived files as needed.  In this way, you can simply leave Ekam running while you work on your code, and get information about errors almost immediately on saving.

Note that this feature works best on Linux.  FreeBSD and OSX lack a scalable way to watch large directory trees.  Ekam will use `kqueue`'s `EVFILT_VNODE` on these systems, but this can be problematic because it requires opening a file descriptor for every file watched.  Particularly on OSX, this can quickly exhaust the per-process file descriptor limit.

### Remote Plugins

Ekam can, while running, export a network interface which allows other programs to query the state of the build, including receiving the task tree and error logs. To support this, you must compile Ekam with Cap'n Proto.

#### Compiling with Cap'n Proto

If you clone the [Cap'n Proto](https://capnproto.org) git repository next to Ekam's, i.e. so that it is found at `../capnproto`, then Ekam will be built with Cap'n Proto. Note that you do not have to build or install Cap'n Proto separately; Cap'n Proto's sources are merged into Ekam's build and built by Ekam. (If you look in the `src` directory, you'll see this is accomplished through some sneaky symlinks.)

#### Ekam Client

If you compile Ekam with Cap'n Proto, then Ekam's `ProtoDashboard.cpp` and `ekam-client` should compile correctly.  This enables the `-n` flag to Ekam, which tells it to allow clients to query its state over the network.  Invoke like:

  ekam -n :41315

Then attach the client like so:

  nc localhost 41315 | ekam-client

(A future version of the client will be able to connect directly rather than use netcat in this way.)

#### Qt Creator Plugin

The `qtcreator` directory in the Ekam repository contains source code for a [Qt Creator](http://qt-project.org/wiki/Category:Tools::QtCreator) plugin. Qt Creator is an open source C++ IDE that, despite its name, is quite nice even for non-Qt code.

To build the Qt Creator plugin, you'll want to download the Qt Creator source code release and build it from scratch. Then, go into the `qtcreator` directory in the Ekam repo and do the following:

    export QTC_SOURCE=<path to qt creator source directory>
    qmake
    make

This should build the plugin and install it directly into your Qt Creator build.

Now you can start Qt Creator and choose the "Ekam Actions" view in the navigation frame. The plugin connects to a local Ekam instance running on port 41315; so, start ekam like this:

  ekam -c -n :41315

The plugin will also add error markers to you source code, visible in the issue tracker. Double-clicking on a failed rule in the tree view will navigate the issues view to the first message from that action, which you can in turn use to navigate to the source location of the error.
