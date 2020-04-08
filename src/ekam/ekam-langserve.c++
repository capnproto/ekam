#include <ekam/langserve.capnp.h>
#include <ekam/dashboard.capnp.h>
#include <kj/main.h>
#include <capnp/compat/json-rpc.h>
#include <kj/async.h>
#include <unistd.h>
#include <kj/io.h>
#include <capnp/serialize-async.h>
#include <kj/map.h>
#include <kj/filesystem.h>
#include <stdlib.h>
#include <kj/encoding.h>

namespace ekam {

typedef unsigned int uint;

class AsyncIoStreamPair final: public kj::AsyncIoStream {
public:
  AsyncIoStreamPair(kj::Own<kj::AsyncInputStream> input,
                    kj::Own<kj::AsyncOutputStream> output)
      : input(kj::mv(input)), output(kj::mv(output)) {}

  kj::Promise<size_t> read(void* buffer, size_t minBytes, size_t maxBytes) override {
    return input->read(buffer, minBytes, maxBytes);
  }
  kj::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return input->tryRead(buffer, minBytes, maxBytes);
  }
  kj::Maybe<uint64_t> tryGetLength() override {
    return input->tryGetLength();
  }
  kj::Promise<uint64_t> pumpTo(
      AsyncOutputStream& output, uint64_t amount = kj::maxValue) override {
    return input->pumpTo(output, amount);
  }

  kj::Promise<void> write(const void* buffer, size_t size) override {
    return output->write(buffer, size);
  }
  kj::Promise<void> write(kj::ArrayPtr<const kj::ArrayPtr<const kj::byte>> pieces) override {
    return output->write(pieces);
  }
  kj::Maybe<kj::Promise<uint64_t>> tryPumpFrom(
      AsyncInputStream& input, uint64_t amount = kj::maxValue) override {
    return output->tryPumpFrom(input, amount);
  }
  kj::Promise<void> whenWriteDisconnected() override {
    return output->whenWriteDisconnected();
  }

  void shutdownWrite() override {
    output = nullptr;
  }
  void abortRead() override {
    input = nullptr;
  }

private:
  kj::Own<kj::AsyncInputStream> input;
  kj::Own<kj::AsyncOutputStream> output;
};

class SourceFile;

struct Message {
  SourceFile* file;
  uint line;
  uint column;
  uint endColumn;
  kj::String text;

  inline bool operator==(const Message& other) const {
    return file == other.file &&
           line == other.line &&
           column == other.column &&
           endColumn == other.endColumn &&
           text == other.text;
  }
  inline uint hashCode() const {
    return kj::hashCode(file, line, column, endColumn, text);
  }

  void exportRange(lsp::Range::Builder range) {
    auto start = range.initStart();
    start.setLine(line == 0 ? 0 : line - 1);
    start.setCharacter(column == 0 ? 0 : column - 1);

    auto end = range.initEnd();
    end.setLine(line == 0 ? 0 : line - 1);
    end.setCharacter(endColumn == 0 ? (column == 0 ? 65536 : column - 1) : endColumn - 1);
  }
};

enum class Severity: uint8_t {
  NOTE = 0,
  ERROR = 1,
  WARNING = 2,
  INFO = 3,
  HINT = 4
};

struct Diagnostic {
  Severity severity;
  Message message;
  kj::Vector<Message> notes;

  inline bool operator==(const Diagnostic& other) const {
    return severity == other.severity &&
           message == other.message;
  }
  inline uint hashCode() const {
    return kj::hashCode(static_cast<uint8_t>(severity), message);
  }
};

class SourceFile;

class DirtySet {
public:
  void add(SourceFile* file) {
    dirty.upsert(file, [](auto...) {});
    KJ_IF_MAYBE(f, fulfiller) {
      f->get()->fulfill();
      fulfiller = nullptr;
    }
  }
  template <typename Func>
  void forEach(Func&& func) {
    for (auto file: dirty) {
      func(*file);
    }
    dirty.clear();
  }

  kj::Maybe<kj::Promise<void>> whenNonEmpty() {
    KJ_REQUIRE(fulfiller == nullptr, "can only call whenNonEmpty() once at a time");
    if (dirty.size() > 0) {
      return kj::Promise<void>(kj::READY_NOW);
    } else if (isShutdown) {
      return nullptr;
    } else {
      auto paf = kj::newPromiseAndFulfiller<void>();
      fulfiller = kj::mv(paf.fulfiller);
      return kj::mv(paf.promise);
    }
  }

  void shutdown() {
    // Make it so calling whenNotEmpty() when empty returns null rather than blocking.
    KJ_IF_MAYBE(f, fulfiller) {
      f->get()->fulfill();
      fulfiller = nullptr;
    }
    isShutdown = true;
  }

private:
  kj::HashSet<SourceFile*> dirty;
  kj::Maybe<kj::Own<kj::PromiseFulfiller<void>>> fulfiller;
  bool isShutdown = false;
};

class SourceFile {
public:
  SourceFile(DirtySet& dirtySet, kj::String realPath)
      : dirtySet(dirtySet), realPath(kj::mv(realPath)) {}
  inline kj::StringPtr getRealPath() { return realPath; }

  void markDirty() {
    dirtySet.add(this);
  }

  void markStale() {
    // We were informed the file has changed, which potentially invalidates all diagnostics.
    // Mark them all stale.
    bool dirty = false;
    for (auto& diagnostic: diagnostics) {
      if (!diagnostic.stale) {
        diagnostic.stale = true;
        dirty = true;
      }
    }
    if (dirty) {
      markDirty();
    }
  }

  Diagnostic& add(Diagnostic&& diagnostic) {
    auto& result = diagnostics.findOrCreate(diagnostic, [&]() {
      dirtySet.add(this);
      return DiagnosticEntry { kj::heap(kj::mv(diagnostic)), 0 };
    });
    ++result.refcount;
    result.stale = false;
    return *result.diagnostic;
  }

  void remove(Diagnostic& diagnostic) {
    auto& entry = KJ_ASSERT_NONNULL(diagnostics.find(diagnostic));
    KJ_ASSERT(entry.diagnostic.get() == &diagnostic);
    if (--entry.refcount == 0) {
      diagnostics.erase(entry);
      dirtySet.add(this);
    }
  }

  void exportDiagnostics(kj::StringPtr uriPrefix,
      lsp::LanguageClient::PublishDiagnosticsParams::Builder builder) {
    builder.setUri(kj::str(uriPrefix, realPath));

    size_t count = 0;
    for (auto& diagnostic: diagnostics) {
      if (!diagnostic.stale) ++count;
    }

    auto list = builder.initDiagnostics(count);
    auto iter = diagnostics.begin();
    for (auto out: list) {
      while (iter->stale) ++iter;
      auto& in = *(iter++)->diagnostic;

      out.setSeverity((uint)in.severity);
      in.message.exportRange(out.initRange());
      out.setMessage(in.message.text);
      out.setSource("ekam");

      if (!in.notes.empty()) {
        auto notes = out.initRelatedInformation(in.notes.size());
        for (auto i: kj::indices(in.notes)) {
          auto outNote = notes[i];
          auto& inNote = in.notes[i];

          auto loc = outNote.initLocation();
          loc.setUri(kj::str(uriPrefix, inNote.file->realPath));
          inNote.exportRange(loc.initRange());
          outNote.setMessage(inNote.text);
        }
      }
    }
  }

private:
  DirtySet& dirtySet;
  kj::String realPath;

  struct DiagnosticEntry {
    kj::Own<Diagnostic> diagnostic;
    uint refcount;
    bool stale = false;

    bool operator==(const Diagnostic& other) const { return *diagnostic == other; }
    bool operator==(const DiagnosticEntry& other) const { return *diagnostic == *other.diagnostic; }
    uint hashCode() const { return kj::hashCode(*diagnostic); }
  };
  kj::HashSet<DiagnosticEntry> diagnostics;
};

class SourceFileSet {
public:
  SourceFileSet(const kj::ReadableDirectory& projectHome, DirtySet& dirtySet)
      : projectHome(projectHome), dirtySet(dirtySet) {}

  kj::Maybe<SourceFile&> get(kj::StringPtr name) {
    static constexpr kj::StringPtr STRIP_PREFIXES[] = {
      "/ekam-provider/canonical/"_kj,
      "/ekam-provider/c++header/"_kj
    };
    for (auto prefix: STRIP_PREFIXES) {
      if (name.startsWith(prefix)) {
        name = name.slice(prefix.size());
        break;
      }
    }

    auto deref = [](kj::Maybe<kj::Own<SourceFile>>& maybe) {
      return maybe.map([](kj::Own<SourceFile>& own) -> SourceFile& { return *own; });
    };

    KJ_IF_MAYBE(existing, files.find(name)) {
      return deref(*existing);
    }

    if (name.startsWith("/")) {
      // Absolute path, probably a header or something.
      return insert(name, kj::heap<SourceFile>(dirtySet, kj::str(name)));
    } else {
      // Look for the file under `src` and `tmp`.
      auto canonical = kj::Path::parse(name);
      static constexpr kj::StringPtr DIRS[] = {"src"_kj, "tmp"_kj};
      for (auto dir: DIRS) {
        auto path = kj::Path({dir}).append(canonical);
        if (projectHome.exists(path)) {
          // Found it.
          return insert(name, kj::heap<SourceFile>(dirtySet,
              expandSymlinks(kj::mv(path)).toString()));
        }
      }

      if (projectHome.exists(canonical)) {
        // Maybe it was already non-canonical.
        return insert(name, kj::heap<SourceFile>(dirtySet,
            expandSymlinks(kj::mv(canonical)).toString()));
      }

      // This path doesn't appear to be a file. Cache this fact.
      files.insert(kj::str(name), nullptr);
      return nullptr;
    }
  }

  kj::Maybe<SourceFile&> findByRealPath(kj::StringPtr path) {
    return filesByPath.find(path).map([](SourceFile* f) -> SourceFile& { return *f; });
  }

  template <typename Func>
  void forEach(Func&& func) {
    for (auto& entry: files) {
      KJ_IF_MAYBE(f, entry.value) {
        func(**f);
      }
    }
  }

private:
  const kj::ReadableDirectory& projectHome;
  DirtySet& dirtySet;
  kj::HashMap<kj::String, kj::Maybe<kj::Own<SourceFile>>> files;
  kj::HashMap<kj::StringPtr, SourceFile*> filesByPath;

  SourceFile& insert(kj::StringPtr name, kj::Own<SourceFile> file) {
    auto& result = *file;
    auto& realPathEntry = filesByPath.insert(file->getRealPath(), file.get());
    KJ_ON_SCOPE_FAILURE(filesByPath.erase(realPathEntry));
    files.insert(kj::str(name), kj::mv(file));
    return result;
  }

  kj::Path expandSymlinks(kj::Path path) {
  retry:
    for (size_t i = 1; i < path.size(); i++) {
      auto parent = path.slice(0, i);
      KJ_IF_MAYBE(link, projectHome.tryReadlink(parent)) {
        if (!link->startsWith("/")) {
          try {
            path = parent.slice(0, i-1).eval(*link).append(path.slice(i, path.size()));
            goto retry;
          } catch (const kj::Exception& e) {
            KJ_LOG(WARNING, "bad symlink", *link, e.getDescription());
          }
        }
      }
    }

    return kj::mv(path);
  }
};

kj::Maybe<uint> tryConsumeNumberColon(kj::StringPtr& str) {
  if (str.startsWith(":")) return nullptr;

  for (size_t i = 0; i < str.size(); i++) {
    if (str[i] == ':') {
      uint result = strtoul(str.cStr(), nullptr, 10);
      str = str.slice(i + 1);
      return result;
    } else if (str[i] < '0' || str[i] > '9') {
      return nullptr;
    }
  }

  return nullptr;
}

struct ColumnRange {
  uint start;
  uint end;
};

kj::Maybe<ColumnRange> tryConsumeRangeColon(kj::StringPtr& str) {
  if (str.startsWith(":")) return nullptr;

  kj::Maybe<uint> start;
  size_t startPos = 0;
  for (size_t i = 0; i < str.size(); i++) {
    if (str[i] == ':') {
      uint result = strtoul(str.cStr() + startPos, nullptr, 10);
      str = str.slice(i + 1);
      KJ_IF_MAYBE(s, start) {
        return ColumnRange { *s, result };
      } else {
        return ColumnRange { result, result };
      }
    } else if (str[i] == '-' && start == nullptr) {
      start = strtoul(str.cStr(), nullptr, 10);
      startPos = i + 1;
    } else if (str[i] < '0' || str[i] > '9') {
      return nullptr;
    }
  }

  return nullptr;
}

void trimLeadingSpace(kj::StringPtr& str) {
  while (str.startsWith(" ")) { str = str.slice(1); }
}

Severity consumeSeverity(kj::StringPtr& line) {
  if (line.startsWith("note:")) {
    line = line.slice(5);
    trimLeadingSpace(line);
    return Severity::NOTE;
  } else {
    struct SeverityPrefix {
      kj::StringPtr prefix;
      Severity severity;
    };
    static constexpr SeverityPrefix SEVERITY_PREFIXES[] = {
      { "error:"_kj, Severity::ERROR },
      { "warning:"_kj, Severity::WARNING },
      { "warn:"_kj, Severity::WARNING },
      { "info:"_kj, Severity::INFO },
      { "hint:"_kj, Severity::HINT },
    };

    for (auto prefix: SEVERITY_PREFIXES) {
      if (line.startsWith(prefix.prefix)) {
        line = line.slice(prefix.prefix.size());
        trimLeadingSpace(line);
        return prefix.severity;
      }
    }
    return Severity::ERROR;
  }
}

class Task {
public:
  Task(proto::TaskUpdate::Reader update, SourceFileSet& files) {}
  ~Task() noexcept(false) {
    clearDiagnostics();
  }

  void update(proto::TaskUpdate::Reader update, SourceFileSet& files) {
    // Invalidate log when the task is deleted or it is re-running or scheduled to re-run.
    if (update.getState() == proto::TaskUpdate::State::PENDING ||
        update.getState() == proto::TaskUpdate::State::RUNNING ||
        update.getState() == proto::TaskUpdate::State::DELETED) {
      clearDiagnostics();
    }

    kj::StringPtr log = update.getLog();

    kj::String ownLog;
    if (leftoverLog.size() > 0) {
      ownLog = kj::str(leftoverLog, log);
      log = ownLog;
    }

    for (;;) {
      kj::StringPtr line;
      kj::String ownLine;
      KJ_IF_MAYBE(eol, log.findFirst('\n')) {
        ownLine = kj::str(log.slice(0, *eol));
        line = ownLine;
        log = log.slice(*eol + 1);
      } else {
        leftoverLog = kj::str(log);
        break;
      }

      trimLeadingSpace(line);
      if (line == nullptr) continue;

      static constexpr kj::StringPtr IGNORE_PREFIXES[] = {
        "In file included from "_kj
      };
      bool ignore = false;
      for (auto prefix: IGNORE_PREFIXES) {
        if (line.startsWith(prefix)) {
          ignore = true;
          break;
        }
      }
      if (ignore) continue;

      static constexpr kj::StringPtr STRIP_PREFIXES[] = {
        // Linker errors start with this.
        "/usr/bin/ld: "_kj
      };
      for (auto prefix: STRIP_PREFIXES) {
        if (line.startsWith(prefix)) {
          line = line.slice(prefix.size());
        }
      }

      size_t spaceAt = line.findFirst(' ').orDefault(line.size());
      if (line[spaceAt - 1] != ':') {
        // No file:line:column: to parse... skip.
        continue;
      }

      // parse file:line:column:
      size_t pos = KJ_ASSERT_NONNULL(line.findFirst(':'));
      auto filename = kj::str(line.slice(0, pos));
      KJ_IF_MAYBE(file, files.get(filename)) {
        line = line.slice(pos + 1);

        kj::Maybe<uint> lineNo = tryConsumeNumberColon(line);
        kj::Maybe<ColumnRange> columnRange = tryConsumeRangeColon(line);

        trimLeadingSpace(line);

        Severity severity = consumeSeverity(line);

        Message message {
          file,
          lineNo.orDefault(0),
          columnRange.map([](ColumnRange c) { return c.start; }).orDefault(0),
          columnRange.map([](ColumnRange c) { return c.end; }).orDefault(0),
          kj::str(line)
        };

        if (severity == Severity::NOTE) {
          // Append note to previous diagnostic.
          if (addNotesToBack) {
            diagnostics.back()->notes.add(kj::mv(message));
            diagnostics.back()->message.file->markDirty();
          }
        } else {
          Diagnostic diagnostic {
            severity, kj::mv(message), {}
          };
          diagnostics.add(&file->add(kj::mv(diagnostic)));

          // If the notes aren't empty, then this must be a dupe diagnostic, and we don't want to
          // add duplicate notes.
          addNotesToBack = diagnostics.back()->notes.empty();
        }
      } else {
        // Doesn't appear to start with a filename. Skip.
        continue;
      }
    }
  }

private:
  kj::Vector<Diagnostic*> diagnostics;
  kj::String leftoverLog;
  bool addNotesToBack = false;

  void clearDiagnostics() {
    for (auto diagnostic: diagnostics) {
      diagnostic->message.file->remove(*diagnostic);
    }
    diagnostics.clear();
    addNotesToBack = false;
  }
};

class LanguageServerImpl final: public lsp::LanguageServer::Server {
public:
  LanguageServerImpl(kj::Own<kj::PromiseFulfiller<void>> initializedFulfiller)
      : initializedFulfiller(kj::mv(initializedFulfiller)) {}
  KJ_DISALLOW_COPY(LanguageServerImpl);

  class Scope {
  public:
    Scope(LanguageServerImpl& server, SourceFileSet& files, kj::StringPtr uriPrefix)
        : server(server), files(files), uriPrefix(uriPrefix) {
      server.scope = this;
    }
    ~Scope() noexcept(false) {
      server.scope = nullptr;
    }

  private:
    LanguageServerImpl& server;
    SourceFileSet& files;
    kj::StringPtr uriPrefix;

    friend class LanguageServerImpl;
  };

protected:
  kj::Promise<void> initialize(InitializeContext context) override {
    initializedFulfiller->fulfill();
    auto sync = context.initResults().initCapabilities().initTextDocumentSync();
    sync.setOpenClose(true);
    sync.setChange(lsp::TextDocumentSyncKind::INCREMENTAL);
    sync.setDidSave(true);
    return kj::READY_NOW;
  }
  kj::Promise<void> shutdown(ShutdownContext context) override {
    return kj::READY_NOW;
  }
  kj::Promise<void> exit(ExitContext context) override {
    _exit(0);
  }

  kj::Promise<void> didOpen(DidOpenContext context) override {
    // Ignore.
    return kj::READY_NOW;
  }
  kj::Promise<void> didClose(DidCloseContext context) override {
    // Ignore.
    return kj::READY_NOW;
  }
  kj::Promise<void> didChange(DidChangeContext context) override {
    KJ_IF_MAYBE(s, scope) {
      auto params = context.getParams();
      auto uri = params.getTextDocument().getUri();
      if (uri.startsWith(s->uriPrefix)) {
        auto path = kj::decodeUriComponent(uri.slice(s->uriPrefix.size()));
        KJ_IF_MAYBE(file, s->files.findByRealPath(path)) {
          file->markStale();
        }
      }
    }
    return kj::READY_NOW;
  }
  kj::Promise<void> didSave(DidSaveContext context) override {
    // Ignore for now.
    // TODO(someday): Start a new location map for this file and interpret future diagnostics
    //   against that map rather than the current one.
    return kj::READY_NOW;
  }

private:
  kj::Own<kj::PromiseFulfiller<void>> initializedFulfiller;
  kj::Maybe<Scope&> scope;
};

class LanguageServerMain {
public:
  LanguageServerMain(kj::ProcessContext& context): context(context) {}

  kj::MainFunc getMain() {
    return kj::MainBuilder(context, "Ekam Language Server",
          "Implements the VS Code Language Server Protocol to report errors from Ekam.")
        .expectArg("<address>", KJ_BIND_METHOD(*this, run))
        .build();
  }

  kj::MainBuilder::Validity run(kj::StringPtr addr) {
    auto io = kj::setupAsyncIo();
    auto fs = kj::newDiskFilesystem();

    auto parsedAddr = io.provider->getNetwork().parseAddress(addr).wait(io.waitScope);

    auto stream = kj::heap<AsyncIoStreamPair>(
        io.lowLevelProvider->wrapInputFd(kj::AutoCloseFd(STDIN_FILENO)),
        io.lowLevelProvider->wrapOutputFd(kj::AutoCloseFd(STDOUT_FILENO)));

    auto initPaf = kj::newPromiseAndFulfiller<void>();

    auto ownServer = kj::heap<LanguageServerImpl>(kj::mv(initPaf.fulfiller));
    LanguageServerImpl& server = *ownServer;

    capnp::JsonRpc::ContentLengthTransport transport(*stream);
    capnp::JsonRpc jsonRpc(transport, capnp::toDynamic(kj::mv(ownServer)));

    auto errorTask = jsonRpc.onError()
        .then([]() {
      KJ_LOG(ERROR, "JsonRpc.onError() resolved normally?");
    }, [](kj::Exception&& exception) {
      KJ_LOG(ERROR, "JSON-RPC connection failed", exception);
    }).attach(kj::defer([]() {
      exit(1);
    })).eagerlyEvaluate(nullptr);

    auto client = jsonRpc.getPeer<lsp::LanguageClient>();

    // Wait until initialized, as vscode will ignore diagnostics delivered too soon.
    initPaf.promise.wait(io.waitScope);

    for (;;) {
      // Repeatedly try to connect to Ekam.
      kj::Own<kj::AsyncIoStream> ekamConnection;
      for (;;) {
        KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
          ekamConnection = parsedAddr->connect().wait(io.waitScope);
        })) {
          if (exception->getType() == kj::Exception::Type::DISCONNECTED) {
            io.provider->getTimer().afterDelay(2 * kj::SECONDS).wait(io.waitScope);
          } else {
            kj::throwFatalException(kj::mv(*exception));
          }
        } else {
          break;
        }
      }

      // Read first message from Ekam.
      kj::String homeUri;
      auto projectHome = ({
        auto message = capnp::readMessage(*ekamConnection).wait(io.waitScope);
        auto header = message->getRoot<proto::Header>();
        auto path = kj::str(header.getProjectRoot());
        homeUri = kj::str("file://", path, '/');
        KJ_ASSERT(path.startsWith("/"));
        fs->getRoot().openSubdir(kj::Path::parse(path.slice(1)));
      });

      DirtySet dirtySet;
      SourceFileSet files(*projectHome, dirtySet);
      kj::HashMap<uint, kj::Own<Task>> tasks;

      LanguageServerImpl::Scope serverScope(server, files, homeUri);
      kj::Promise<void> updateLoopTask = updateLoop(dirtySet, client, homeUri);

      for (;;) {
        kj::Own<capnp::MessageReader> message;
        KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
          message = capnp::readMessage(*ekamConnection).wait(io.waitScope);
        })) {
          if (exception->getType() == kj::Exception::Type::DISCONNECTED) {
            // Disconnected, start over.
            break;
          } else {
            kj::throwFatalException(kj::mv(*exception));
          }
        }
        auto update = message->getRoot<proto::TaskUpdate>();

        if (update.getState() == proto::TaskUpdate::State::DELETED) {
          tasks.erase(update.getId());
        } else {
          auto& task = *tasks.findOrCreate(update.getId(), [&]() {
            return kj::HashMap<uint, kj::Own<Task>>::Entry {
              update.getId(), kj::heap<Task>(update, files)
            };
          });
          task.update(update, files);
        }
      }

      // Clear all diagnostics.
      tasks.clear();
      dirtySet.shutdown();
      updateLoopTask.wait(io.waitScope);
    }
  }

private:
  kj::ProcessContext& context;

  kj::Promise<void> updateLoop(DirtySet& dirtySet,
      lsp::LanguageClient::Client client, kj::StringPtr homeUri) {
    KJ_IF_MAYBE(p, dirtySet.whenNonEmpty()) {
      return p->then([]() {
        // Delay for other messages.
        return kj::evalLast([]() {});
      }).then([&dirtySet, client, homeUri]() mutable {
        kj::Vector<kj::Promise<void>> promises;
        dirtySet.forEach([&](SourceFile& file) {
          auto req = client.publishDiagnosticsRequest();
          file.exportDiagnostics(homeUri, req);
          promises.add(req.send().ignoreResult());
        });
        return kj::joinPromises(promises.releaseAsArray());
      }).then([this, &dirtySet, client, homeUri]() mutable {
        return updateLoop(dirtySet, kj::mv(client), homeUri);
      });
    } else {
      return kj::READY_NOW;
    }
  }
};

}  // namespace ekam

KJ_MAIN(ekam::LanguageServerMain);
