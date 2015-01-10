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

#include "dashboard.pb.h"

namespace ProjectExplorer {
class TaskHub;
}

namespace EkamDashboard {
namespace Internal {

class EkamDashboardPlugin;

class ActionState: public QObject {
  Q_OBJECT

public:
  ActionState(EkamDashboardPlugin* plugin, const ekam::proto::TaskUpdate& initialUpdate);
  ~ActionState();

  // Returns true if the action went from silent to non-silent, and thus a newAction event should
  // be fired.
  void applyUpdate(const ekam::proto::TaskUpdate& update);

  bool isDead() {
    return state == ekam::proto::TaskUpdate::DELETED;
  }

  ekam::proto::TaskUpdate::State getState() { return state; }
  const QString& getVerb() { return verb; }
  const QString& getNoun() { return noun; }
  const QString& getPath() { return path; }
  bool isHidden() {
    return silent && state != ekam::proto::TaskUpdate::FAILED;
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
  std::string leftoverLog;
  QList<ProjectExplorer::Task> tasks;

  void clearTasks();
  void consumeLog(const std::string& log);
  void parseLogLine(QString line);
};

class EkamDashboardPlugin : public ExtensionSystem::IPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "EkamDashboard.json")
    
public:
  EkamDashboardPlugin();
  ~EkamDashboardPlugin();

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
  ProjectExplorer::TaskHub* hub;
  QTcpSocket* socket;
  QByteArray buffer;
  bool seenHeader;
  QString projectRoot;
  QHash<int, ActionState*> actions;

  void tryConnect();
  void consumeMessage(const void* data, int size);
  void clearActions();
};

} // namespace Internal
} // namespace EkamDashboard

#endif // EKAMDASHBOARD_H

