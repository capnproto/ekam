// Kenton's Code Playground -- http://code.google.com/p/kentons-code
// Author: Kenton Varda (temporal@gmail.com)
// Copyright (c) 2010 Google, Inc. and contributors.
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

#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/io/coded_stream.h>

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

QString toQString(const std::string& str) {
  return QString::fromUtf8(str.data(), str.size());
}

EkamDashboardPlugin::EkamDashboardPlugin()
  : hub(0), socket(0), seenHeader(false) {
  // Create your members
}

EkamDashboardPlugin::~EkamDashboardPlugin() {
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
  Core::ActionManager *am = Core::ICore::actionManager();

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

  hub->addCategory(Core::Id(Constants::TASK_CATEGORY_ID), "Ekam task");

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
      ProjectExplorer::Task::Error, "test error",
      Utils::FileName::fromUserInput(QLatin1String("/home/kenton/code/src/base/OwnedPtr.h")), 10,
      Core::Id(Constants::TASK_CATEGORY_ID)));
}

void EkamDashboardPlugin::socketError(QAbstractSocket::SocketError error) {
//  qDebug() << "Socket error: " << error;
  clearActions();
  QTimer::singleShot(5000, this, SLOT(retryConnection()));
}

void EkamDashboardPlugin::retryConnection() {
  tryConnect();
}

void EkamDashboardPlugin::socketReady() {
  buffer += socket->readAll();
//  qDebug() << "Received data.  buffer.size() = " << buffer.size();

  while (true) {
    int i = 0;
    int size = 0;

    // Parse the size.
    while (true) {
      if (i >= buffer.size()) {
        // Size is incomplete.
        return;
      }

      unsigned char b = buffer.at(i);
      size |= (b & 0x7f) << (7 * i);
      ++i;
      if ((b & 0x80) == 0) {
        break;
      }
    }

    if (i + size > buffer.size()) {
      // Message is incomplete.
      return;
    }

    consumeMessage(buffer.data() + i, size);
    buffer.remove(0, i + size);
  }
}

void EkamDashboardPlugin::tryConnect() {
  seenHeader = false;
//  qDebug() << "Trying to connect...";
  socket->connectToHost("localhost", 51315);
}

void EkamDashboardPlugin::consumeMessage(const void* data, int size) {
  if (!seenHeader) {
    seenHeader = true;

    ekam::proto::Header header;
    header.ParseFromArray(data, size);
  //  qDebug() << "Received header: " << header.DebugString().c_str();
    projectRoot = toQString(header.project_root());
  } else {
    ekam::proto::TaskUpdate update;
    update.ParseFromArray(data, size);
  //  qDebug() << "Received task update: " << update.DebugString().c_str();

    ActionState*& slot = actions[update.id()];
    if (slot == 0) {
      slot = new ActionState(this, update);
    } else {
      slot->applyUpdate(update);
    }

    if (slot->isDead()) {
      delete slot;
      actions.remove(update.id());
    }
  }
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
    EkamDashboardPlugin *plugin, const ekam::proto::TaskUpdate &initialUpdate)
  : plugin(plugin), state(initialUpdate.state()),
    verb(toQString(initialUpdate.verb())),
    noun(toQString(initialUpdate.noun())),
    path(plugin->findFile(noun)),
    silent(initialUpdate.silent()) {
  if (!initialUpdate.log().empty()) {
    consumeLog(initialUpdate.log());
  }
  if (!isHidden()) {
    plugin->unhideAction(this);
  }
}

ActionState::~ActionState() {
  emit removed();
  clearTasks();
}

void ActionState::applyUpdate(const ekam::proto::TaskUpdate& update) {
  if (update.has_state() && update.state() != state) {
    bool wasHidden = isHidden();

    // If state was previously BLOCKED, and we managed to un-block, then we don't care about the
    // reason why we were blocked, so clear the text.
    if (state == ekam::proto::TaskUpdate::BLOCKED &&
        (update.state() == ekam::proto::TaskUpdate::PENDING ||
         update.state() == ekam::proto::TaskUpdate::RUNNING)) {
      clearTasks();
    }

    state = update.state();

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
  if (!update.log().empty()) {
    consumeLog(update.log());
  }
  if (state != ekam::proto::TaskUpdate::RUNNING && !leftoverLog.empty()) {
    parseLogLine(toQString(leftoverLog));
    leftoverLog.clear();
  }
}

void ActionState::clearTasks() {
  emit clearedTasks();
  foreach (const ProjectExplorer::Task& task, tasks) {
    plugin->taskHub()->removeTask(task);
  }
  tasks.clear();
  leftoverLog.clear();
}

void ActionState::consumeLog(const std::string& log) {
  std::string::size_type pos = 0;
  while (true) {
    std::string::size_type next = log.find_first_of('\n', pos);
    if (next == std::string::npos) {
      leftoverLog += log.substr(pos);
      return;
    }

    leftoverLog += log.substr(pos, next - pos);
    pos = next + 1;

    parseLogLine(toQString(leftoverLog));
    leftoverLog.clear();
  }
}

void ActionState::parseLogLine(QString line) {
  static const QRegExp FILE("^([^ :]+):(.*)");
  static const QRegExp INDEX("^([0-9]+):(.*)");
  static const QRegExp WARNING(" *warning:(.*)", Qt::CaseInsensitive);
  static const QRegExp NOTE(" *note:(.*)", Qt::CaseInsensitive);

  ProjectExplorer::Task::TaskType type = ProjectExplorer::Task::Unknown;
  QString file;
  int lineNo = -1;
  int columnNo = -1;

  // OMGWTF matching a QRegExp modifies the QRegExp object rather than returning some sort of match
  // object, so we must make copies.  Hopefully using the copy constructor rather than constructing
  // directly from the pattern strings means they won't be re-compiled every time.
  QRegExp fileRe = FILE;
  if (fileRe.exactMatch(line)) {
    file = fileRe.capturedTexts()[1];
    if (file.startsWith(QLatin1String("/ekam-provider/c++header/"))) {
      file.remove(0, strlen("/ekam-provider/c++header/"));
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
    type = ProjectExplorer::Task::Error;
    lineNo = indexRe.capturedTexts()[1].toInt();
    line = indexRe.capturedTexts()[2];
    if (indexRe.exactMatch(line)) {
      columnNo = indexRe.capturedTexts()[1].toInt();
      line = indexRe.capturedTexts()[2];
    }
  }

  QRegExp warningRe = WARNING;
  QRegExp noteRe = NOTE;
  if (warningRe.exactMatch(line)) {
    type = ProjectExplorer::Task::Warning;
    line = warningRe.capturedTexts()[1];
  } else if (noteRe.exactMatch(line)) {
    type = ProjectExplorer::Task::Unknown;
    line = noteRe.capturedTexts()[1];
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

Q_EXPORT_PLUGIN2(EkamDashboard, EkamDashboard::Internal::EkamDashboardPlugin)
