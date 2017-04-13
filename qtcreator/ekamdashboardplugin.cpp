// Ekam Build System
// Author: Kenton Varda (kenton@sandstorm.io)
// Copyright (c) 2010-2015 Kenton Varda, Google Inc., and contributors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ekamdashboardplugin.h"
#include "ekamdashboardconstants.h"
#include "ekamtreewidget.h"

#include <capnp/serialize-async.h>
#include <kj/debug.h>
#include <deque>

#include <extensionsystem/pluginmanager.h>

#include <projectexplorer/taskhub.h>

#include <QAction>
#include <QMessageBox>
#include <QMainWindow>
#include <QMenu>
#include <QDebug>
#include <QTimer>
#include <QFile>
#include <QRegExp>

#include <QtPlugin>

namespace EkamDashboard {
namespace Internal {

QString toQString(kj::ArrayPtr<const char> str) {
  return QString::fromUtf8(str.begin(), str.size());
}

class EkamDashboardPlugin::FakeAsyncInput final: public kj::AsyncInputStream {
public:
  kj::Promise<size_t> read(void* buffer, size_t minBytes, size_t maxBytes) override {
    return tryRead(buffer, minBytes, maxBytes).then([=](size_t result) {
      KJ_REQUIRE(result >= minBytes, "Premature EOF") {
        // Pretend we read zeros from the input.
        memset(reinterpret_cast<kj::byte*>(buffer) + result, 0, minBytes - result);
        return minBytes;
      }
      return result;
    });
  }

  kj::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    Request request(buffer, minBytes, maxBytes);

    if (byteQueue.size() > 0) {
      KJ_IF_MAYBE(size, request.consumeFrom(byteQueue)) {
        return *size;
      }
    }

    requests.push_back(kj::mv(request));
    return requests.back().finishLater();
  }

  void add(QByteArray bytes) {
    while (!requests.empty()) {
      auto& request = requests.front();

      KJ_IF_MAYBE(size, request.consumeFrom(bytes)) {
        request.fulfill(*size);
        requests.pop_front();
      } else {
        // Not enough bytes to satisfy the request.
        return;
      }
    }

    byteQueue.append(bytes);
  }

private:
  class Request {
  public:
    Request(void* buffer, size_t minBytes, size_t maxBytes)
        : pos(reinterpret_cast<kj::byte*>(buffer)),
          minLeft(minBytes), maxLeft(maxBytes),
          alreadyRead(0) {}

    kj::Maybe<size_t> consumeFrom(QByteArray& bytes) {
      size_t n = kj::min(maxLeft, bytes.size());
      memcpy(pos, bytes.data(), n);

      bytes.remove(0, n);

      if (n >= minLeft) {
        return alreadyRead + n;
      } else {
        pos += n;
        minLeft -= n;
        maxLeft -= n;
        alreadyRead += n;
        return nullptr;
      }
    }

    kj::Promise<size_t> finishLater() {
      auto paf = kj::newPromiseAndFulfiller<size_t>();
      fulfiller = kj::mv(paf.fulfiller);
      return kj::mv(paf.promise);
    }

    void fulfill(size_t amount) {
      fulfiller->fulfill(kj::mv(amount));
    }

  private:
    kj::byte* pos;
    size_t minLeft;
    size_t maxLeft;
    size_t alreadyRead;
    kj::Own<kj::PromiseFulfiller<size_t>> fulfiller;
  };

  std::deque<Request> requests;
  QByteArray byteQueue;
};

EkamDashboardPlugin::EkamDashboardPlugin()
    : hub(0), socket(0), seenHeader(false), waitScope(eventLoop),
    fakeInput(kj::heap<FakeAsyncInput>()), readTask(nullptr) {}

EkamDashboardPlugin::~EkamDashboardPlugin() noexcept {
  // Unregister objects from the plugin manager's object pool
  // Delete members
}

bool EkamDashboardPlugin::initialize(const QStringList &arguments, QString *errorString) {
  // Register objects in the plugin manager's object pool
  // Load settings
  // Add actions to menus
  // Connect to other plugins' signals
  // In the initialize method, a plugin can be sure that the plugins it
  // depends on have initialized their members.
  
  Q_UNUSED(arguments)
  Q_UNUSED(errorString)
  Core::ActionManager *am = Core::ActionManager::instance();

  QAction *action = new QAction(tr("EkamDashboard action"), this);
  Core::Command *cmd = am->registerAction(action, Constants::ACTION_ID,
                                          Core::Context(Core::Constants::C_GLOBAL));
  cmd->setDefaultKeySequence(QKeySequence(tr("Ctrl+Alt+Meta+A")));
  connect(action, SIGNAL(triggered()), this, SLOT(triggerAction()));

  Core::ActionContainer *menu = am->createMenu(Constants::MENU_ID);
  menu->menu()->setTitle(tr("EkamDashboard"));
  menu->addAction(cmd);
  am->actionContainer(Core::Constants::M_TOOLS)->addMenu(menu);

  addAutoReleasedObject(new EkamTreeWidgetFactory(this));

  return true;
}

void EkamDashboardPlugin::extensionsInitialized() {
  // Retrieve objects from the plugin manager's object pool
  // In the extensionsInitialized method, a plugin can be sure that all
  // plugins that depend on it are completely initialized.

  ExtensionSystem::PluginManager *pm = ExtensionSystem::PluginManager::instance();
  hub = pm->getObject<ProjectExplorer::TaskHub>();

  hub->addCategory(Core::Id(Constants::TASK_CATEGORY_ID), QLatin1String("Ekam task"));

  socket = new QTcpSocket(this);
  connect(socket, SIGNAL(error(QAbstractSocket::SocketError)),
          this, SLOT(socketError(QAbstractSocket::SocketError)));
  connect(socket, SIGNAL(readyRead()), this, SLOT(socketReady()));
  tryConnect();
}

ExtensionSystem::IPlugin::ShutdownFlag EkamDashboardPlugin::aboutToShutdown() {
  // Save settings
  // Disconnect from signals that are not needed during shutdown
  // Hide UI (if you add UI that is not in the main window directly)

  delete socket;
  socket = 0;

  clearActions();

  return SynchronousShutdown;
}

void EkamDashboardPlugin::triggerAction() {
//  qDebug() << "triggerAction() qdebug";
  QMessageBox::information(Core::ICore::mainWindow(),
                           tr("Action triggered"),
                           tr("This is an action from EkamDashboard."));

  hub->addTask(ProjectExplorer::Task(
      ProjectExplorer::Task::Error, QLatin1String("test error"),
      Utils::FileName::fromUserInput(QLatin1String("/home/kenton/code/src/base/OwnedPtr.h")), 10,
      Core::Id(Constants::TASK_CATEGORY_ID)));
}

void EkamDashboardPlugin::socketError(QAbstractSocket::SocketError error) {
//  qDebug() << "Socket error: " << error;
  reset();
}

void EkamDashboardPlugin::reset() {
  if (!resetting) {
    resetting = true;
    clearActions();
    QTimer::singleShot(5000, this, SLOT(retryConnection()));
  }
}

void EkamDashboardPlugin::retryConnection() {
  resetting = false;

  // Cancel any async parsing still happening.
  readTask = nullptr;

  // Reset the fake input to clear out its buffers.
  fakeInput = kj::heap<FakeAsyncInput>();

  tryConnect();
}

void EkamDashboardPlugin::socketReady() {
  fakeInput->add(socket->readAll());
  eventLoop.run();
}

void EkamDashboardPlugin::tryConnect() {
  seenHeader = false;
//  qDebug() << "Trying to connect...";
  socket->connectToHost(QLatin1String("localhost"), 41315);
  readTask = messageLoop().eagerlyEvaluate(
      [this](kj::Exception&& e){ KJ_LOG(ERROR, e); reset(); });
}

kj::Promise<void> EkamDashboardPlugin::messageLoop() {
  return capnp::readMessage(*fakeInput).then([this](kj::Own<capnp::MessageReader>&& message) {
    if (!seenHeader) {
      seenHeader = true;

      ekam::proto::Header::Reader header = message->getRoot<ekam::proto::Header>();
    //  qDebug() << "Received header: " << kj::str(header).cStr();
      projectRoot = toQString(header.getProjectRoot());
    } else {
      ekam::proto::TaskUpdate::Reader update = message->getRoot<ekam::proto::TaskUpdate>();
    //  qDebug() << "Received task update: " << kj::str(update).cStr();

      ActionState*& slot = actions[update.getId()];
      if (slot == 0) {
        slot = new ActionState(this, update);
      } else {
        slot->applyUpdate(update);
      }

      if (slot->isDead()) {
        delete slot;
        actions.remove(update.getId());
      }
    }

    return messageLoop();
  });
}

QString EkamDashboardPlugin::findFile(const QString& canonicalPath) {
  QString srcpath = projectRoot + QLatin1String("/src/") + canonicalPath;
  QString tmppath = projectRoot + QLatin1String("/tmp/") + canonicalPath;
  if (QFile::exists(srcpath)) {
    return srcpath;
  } else if (QFile::exists(tmppath)) {
    return tmppath;
  } else {
    return canonicalPath;
  }
}

QList<ActionState*> EkamDashboardPlugin::allActions() {
  QList<ActionState*> result;

  foreach (ActionState* action, actions) {
    if (!action->isHidden()) {
      result << action;
    }
  }

  return result;
}

void EkamDashboardPlugin::clearActions() {
  foreach (ActionState* action, actions) {
    delete action;
  }
  actions.clear();
}

// =======================================================================================

ActionState::ActionState(
    EkamDashboardPlugin *plugin, ekam::proto::TaskUpdate::Reader initialUpdate)
  : plugin(plugin), state(initialUpdate.getState()),
    verb(toQString(initialUpdate.getVerb())),
    noun(toQString(initialUpdate.getNoun())),
    path(plugin->findFile(noun)),
    silent(initialUpdate.getSilent()) {
  auto log = initialUpdate.getLog();
  if (log != nullptr) {
    consumeLog(log);
  }
  if (!isHidden()) {
    plugin->unhideAction(this);
  }
}

ActionState::~ActionState() noexcept {
  emit removed();
  clearTasks();
}

void ActionState::applyUpdate(ekam::proto::TaskUpdate::Reader update) {
  if (update.getState() != ekam::proto::TaskUpdate::State::UNCHANGED &&
      update.getState() != state) {
    bool wasHidden = isHidden();

    // Invalidate log when the task is deleted or it is re-running or scheduled to re-run.
    if (update.getState() == ekam::proto::TaskUpdate::State::PENDING ||
        update.getState() == ekam::proto::TaskUpdate::State::RUNNING ||
        update.getState() == ekam::proto::TaskUpdate::State::DELETED) {
      clearTasks();
    }

    state = update.getState();

    if (isHidden()) {
      if (wasHidden) {
        emit removed();
      }
    } else {
      if (wasHidden) {
        plugin->unhideAction(this);
      } else {
        emit stateChanged(state);
      }
    }
  }
  auto log = update.getLog();
  if (log != nullptr) {
    consumeLog(log);
  }
  if (state != ekam::proto::TaskUpdate::State::RUNNING && !leftoverLog.empty()) {
    parseLogLine(toQString(leftoverLog));
    leftoverLog.resize(0);
  }
}

void ActionState::clearTasks() {
  emit clearedTasks();
  foreach (const ProjectExplorer::Task& task, tasks) {
    plugin->taskHub()->removeTask(task);
  }
  tasks.clear();
  leftoverLog.resize(0);
}

void ActionState::consumeLog(kj::StringPtr log) {
  while (true) {
    KJ_IF_MAYBE(pos, log.findFirst('\n')) {
      leftoverLog.addAll(log.begin(), log.begin() + *pos);
      log = log.slice(*pos + 1);
      parseLogLine(toQString(leftoverLog));
      leftoverLog.resize(0);
    } else {
      leftoverLog.addAll(log);
      return;
    }
  }
}

void ActionState::parseLogLine(QString line) {
  if (tasks.size() > 100) return;  // avoid performance problems with too many tasks

  static const QRegExp FILE(QLatin1String("^([^ :]+):(.*)"));
  static const QRegExp INDEX(QLatin1String("^([0-9]+):(.*)"));
  static const QRegExp WARNING(QLatin1String("(.*[^a-zA-Z0-9])?warning:(.*)"), Qt::CaseInsensitive);
  static const QRegExp ERROR(QLatin1String("(.*[^a-zA-Z0-9])?error:(.*)"), Qt::CaseInsensitive);
  static const QRegExp FAILURE(QLatin1String(" *failure *"), Qt::CaseInsensitive);
  static const QRegExp FULL_LOG(QLatin1String("full log: tmp/(.*)"));

  ProjectExplorer::Task::TaskType type = ProjectExplorer::Task::Unknown;
  QString file;
  int lineNo = -1;
  int columnNo = -1;

  // OMGWTF matching a QRegExp modifies the QRegExp object rather than returning some sort of match
  // object, so we must make copies.  Hopefully using the copy constructor rather than constructing
  // directly from the pattern strings means they won't be re-compiled every time.
  QRegExp fullLog = FULL_LOG;
  QRegExp fileRe = FILE;
  if (fullLog.exactMatch(line)) {
    file = fullLog.capturedTexts()[1];
    file = plugin->findFile(file);
    if (!file.startsWith(QLatin1Char('/'))) {
      file.clear();
    }
  } else if (fileRe.exactMatch(line)) {
    file = fileRe.capturedTexts()[1];
    if (file.startsWith(QLatin1String("/ekam-provider/c++header/"))) {
      file.remove(0, strlen("/ekam-provider/c++header/"));
    } else if (file.startsWith(QLatin1String("/ekam-provider/canonical/"))) {
      file.remove(0, strlen("/ekam-provider/canonical/"));
    }
    file = plugin->findFile(file);
    if (file.startsWith(QLatin1Char('/'))) {
      line = fileRe.capturedTexts()[2];
    } else {
      // Failed to find the file on disk.  Maybe it's not actually a file.  Leave it in the error
      // text.
      file.clear();
    }
  }

  QRegExp indexRe = INDEX;
  if (indexRe.exactMatch(line)) {
    type = ProjectExplorer::Task::Unknown;
    lineNo = indexRe.capturedTexts()[1].toInt();
    line = indexRe.capturedTexts()[2];
    if (indexRe.exactMatch(line)) {
      columnNo = indexRe.capturedTexts()[1].toInt();
      line = indexRe.capturedTexts()[2];
    }
  }

  QRegExp errorRe = ERROR;
  QRegExp warningRe = WARNING;
  QRegExp failureRe = FAILURE;
  if (errorRe.exactMatch(line)) {
    type = ProjectExplorer::Task::Error;
  } else if (warningRe.exactMatch(line)) {
    type = ProjectExplorer::Task::Warning;
  } else if (failureRe.exactMatch(line)) {
    type = ProjectExplorer::Task::Error;
  }

  // Qt Creator tasks don't support column numbers, so add it back into the error text.
  if (columnNo != -1) {
    line = QLatin1String("(col ") + QString::number(columnNo) + QLatin1String(") ") + line;
  }

  tasks.append(ProjectExplorer::Task(type, line, Utils::FileName::fromUserInput(file), lineNo,
                                     Core::Id(Constants::TASK_CATEGORY_ID)));
  plugin->taskHub()->addTask(tasks.back());
  emit addedTask(tasks.back());
}

} // namespace Internal
} // namespace EkamDashboard

