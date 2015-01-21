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

#ifndef EKAMDASHBOARD_H
#define EKAMDASHBOARD_H

#include "ekamdashboard_global.h"

#include <extensionsystem/iplugin.h>

#include <coreplugin/icore.h>
#include <coreplugin/icontext.h>
#include <coreplugin/idocument.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/coreconstants.h>

#include <projectexplorer/task.h>

#include <QtNetwork/QTcpSocket>
#include <QByteArray>
#include <QHash>
#include <QList>

#include <kj/vector.h>
#include <kj/async.h>
#include "dashboard.capnp.h"

namespace ProjectExplorer {
class TaskHub;
}

namespace EkamDashboard {
namespace Internal {

class EkamDashboardPlugin;

class ActionState: public QObject {
  Q_OBJECT

public:
  ActionState(EkamDashboardPlugin* plugin, ekam::proto::TaskUpdate::Reader initialUpdate);
  ~ActionState() noexcept;

  // Returns true if the action went from silent to non-silent, and thus a newAction event should
  // be fired.
  void applyUpdate(ekam::proto::TaskUpdate::Reader update);

  bool isDead() {
    return state == ekam::proto::TaskUpdate::State::DELETED;
  }

  ekam::proto::TaskUpdate::State getState() { return state; }
  const QString& getVerb() { return verb; }
  const QString& getNoun() { return noun; }
  const QString& getPath() { return path; }
  bool isHidden() {
    return silent && state != ekam::proto::TaskUpdate::State::FAILED;
  }
  ProjectExplorer::Task* firstTask() { return tasks.empty() ? 0 : &tasks.first(); }

signals:
  void removed();
  void stateChanged(ekam::proto::TaskUpdate::State state);
  void clearedTasks();
  void addedTask(const ProjectExplorer::Task& task);

private:
  EkamDashboardPlugin* plugin;
  ekam::proto::TaskUpdate::State state;
  QString verb;
  QString noun;
  QString path;
  bool silent;
  kj::Vector<char> leftoverLog;
  QList<ProjectExplorer::Task> tasks;

  void clearTasks();
  void consumeLog(kj::StringPtr log);
  void parseLogLine(QString line);
};

class EkamDashboardPlugin : public ExtensionSystem::IPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "EkamDashboard.json")
    
public:
  EkamDashboardPlugin();
  ~EkamDashboardPlugin() noexcept;

  bool initialize(const QStringList &arguments, QString *errorString);
  void extensionsInitialized();
  ShutdownFlag aboutToShutdown();

  ProjectExplorer::TaskHub* taskHub() { return hub; }
  QString findFile(const QString& canonicalPath);

  QList<ActionState*> allActions();

  void unhideAction(ActionState* action) {
    emit newAction(action);
  }

signals:
  void newAction(ActionState* action);

private slots:
  void triggerAction();
  void socketError(QAbstractSocket::SocketError);
  void retryConnection();
  void socketReady();

private:
  class FakeAsyncInput;

  ProjectExplorer::TaskHub* hub;
  QTcpSocket* socket;
  bool seenHeader;
  QString projectRoot;
  QHash<int, ActionState*> actions;
  kj::EventLoop eventLoop;
  kj::WaitScope waitScope;
  kj::Own<FakeAsyncInput> fakeInput;
  kj::Promise<void> readTask;
  bool resetting = false;

  void reset();
  void tryConnect();
  kj::Promise<void> messageLoop();
  void clearActions();
};

} // namespace Internal
} // namespace EkamDashboard

#endif // EKAMDASHBOARD_H

