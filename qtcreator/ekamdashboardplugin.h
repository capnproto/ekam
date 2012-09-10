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

  void applyUpdate(const ekam::proto::TaskUpdate& update);
  bool isDead() {
    return state == ekam::proto::TaskUpdate::DELETED;
  }

  ekam::proto::TaskUpdate::State getState() { return state; }
  const QString& getVerb() { return verb; }
  const QString& getNoun() { return noun; }
  const QString& getPath() { return path; }
  bool isSilent() { return silent; }
  ProjectExplorer::Task* firstTask() { return tasks.empty() ? 0 : &tasks.first(); }

signals:
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
    
public:
  EkamDashboardPlugin();
  ~EkamDashboardPlugin();

  bool initialize(const QStringList &arguments, QString *errorString);
  void extensionsInitialized();
  ShutdownFlag aboutToShutdown();

  ProjectExplorer::TaskHub* taskHub() { return hub; }
  QString findFile(const QString& canonicalPath);

  const QHash<int, ActionState*> allActions() { return actions; }

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
};

} // namespace Internal
} // namespace EkamDashboard

#endif // EKAMDASHBOARD_H

