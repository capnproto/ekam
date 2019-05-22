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

class LanguageServerImpl final: public lsp::LanguageServer::Server {
public:
  LanguageServerImpl(kj::Own<kj::PromiseFulfiller<void>> initializedFulfiller)
      : initializedFulfiller(kj::mv(initializedFulfiller)) {}
  KJ_DISALLOW_COPY(LanguageServerImpl);

protected:
  kj::Promise<void> initialize(InitializeContext context) override {
    initializedFulfiller->fulfill();
    context.initResults().initCapabilities();
    return kj::READY_NOW;
  }
  kj::Promise<void> shutdown(ShutdownContext context) override {
    return kj::READY_NOW;
  }
  kj::Promise<void> exit(ExitContext context) override {
    _exit(0);
  }

private:
  kj::Own<kj::PromiseFulfiller<void>> initializedFulfiller;
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
  }
  template <typename Func>
  void forEach(Func&& func) {
    for (auto file: dirty) {
      func(*file);
    }
    dirty.clear();
  }

private:
  kj::HashSet<SourceFile*> dirty;
};

class SourceFile {
public:
  SourceFile(DirtySet& dirtySet, kj::String realPath)
      : dirtySet(dirtySet), realPath(kj::mv(realPath)) {}
  inline kj::StringPtr getRealPath() { return realPath; }

  void markDirty() {
    dirtySet.add(this);
  }

  Diagnostic& add(Diagnostic&& diagnostic) {
    auto& result = diagnostics.findOrCreate(diagnostic, [&]() {
      dirtySet.add(this);
      return DiagnosticEntry { kj::heap(kj::mv(diagnostic)), 0 };
    });
    ++result.refcount;
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

    auto list = builder.initDiagnostics(diagnostics.size());
    auto iter = diagnostics.begin();
    for (auto out: list) {
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

    bool operator==(const Diagnostic& other) const { return *diagnostic == other; }
    bool operator==(const DiagnosticEntry& other) const { return *diagnostic == *other.diagnostic; }
    uint hashCode() const { return kj::hashCode(*diagnostic); }
  };
  kj::HashSet<DiagnosticEntry> diagnostics;
};

class SourceFileSet {
  typedef kj::HashMap<kj::String, kj::Maybe<kj::Own<SourceFile>>> FileMap;
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
      auto& entry = files.insert(kj::str(name), kj::heap<SourceFile>(dirtySet, kj::str(name)));
      return deref(entry.value);
    } else {
      // Look for the file under `src` and `tmp`.
      auto canonical = kj::Path::parse(name);
      static constexpr kj::StringPtr DIRS[] = {"src"_kj, "tmp"_kj};
      for (auto dir: DIRS) {
        if (projectHome.exists(kj::Path({dir}).append(canonical))) {
          // Found it.
          auto& entry = files.insert(kj::str(name),
              kj::heap<SourceFile>(dirtySet, kj::str(dir, '/', name)));
          return deref(entry.value);
        }
      }

      if (projectHome.exists(canonical)) {
        // Maybe it was already non-canonical.
        auto& entry = files.insert(kj::str(name), kj::heap<SourceFile>(dirtySet, kj::str(name)));
        return deref(entry.value);
      }

      // This path doesn't appear to be a file. Cache this fact.
      files.insert(kj::str(name), nullptr);
      return nullptr;
    }
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
  FileMap files;
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

    capnp::JsonRpc::ContentLengthTransport transport(*stream);
    capnp::JsonRpc jsonRpc(transport, capnp::toDynamic(
          kj::heap<LanguageServerImpl>(kj::mv(initPaf.fulfiller))));

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

      auto updateClient = [&]() {
        kj::Vector<kj::Promise<void>> promises;
        dirtySet.forEach([&](SourceFile& file) {
          auto req = client.publishDiagnosticsRequest();
          file.exportDiagnostics(homeUri, req);
          promises.add(req.send().ignoreResult());
        });
        kj::joinPromises(promises.releaseAsArray()).wait(io.waitScope);
      };

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

        updateClient();
      }

      // Clear all diagnostics.
      tasks.clear();
      updateClient();
    }
  }

private:
  kj::ProcessContext& context;
};

}  // namespace ekam

KJ_MAIN(ekam::LanguageServerMain);
