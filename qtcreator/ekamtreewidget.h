#ifndef EKAMTREEWIDGET_H
#define EKAMTREEWIDGET_H

#include <coreplugin/inavigationwidgetfactory.h>

#include <QWidget>
#include <QAbstractItemModel>

#include "dashboard.pb.h"

namespace EkamDashboard {
namespace Internal {

class EkamDashboardPlugin;
class ActionState;

class EkamTreeNode : public QObject {
  Q_OBJECT
public:
  EkamTreeNode();
  EkamTreeNode(EkamTreeNode* parent, const QString& name);
  virtual ~EkamTreeNode();

  const QString& name() { return name_; }
  ActionState* action() { return action_; }
  void setAction(ActionState* newAction);

private:
  QString name_;

  // Can be null for folder nodes.
  ActionState* action_;

  // The EkamTreeNode uses QObject's parent and child pointers to track related nodes.
};

class EkamTreeModel : public QAbstractItemModel {
  Q_OBJECT
public:
  EkamTreeModel(EkamDashboardPlugin* plugin, QObject* parent = 0);
  virtual ~EkamTreeModel();

  virtual QModelIndex index(int row, int column, const QModelIndex & parent = QModelIndex()) const;
  virtual QModelIndex parent(const QModelIndex &index) const;
  virtual QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;

  virtual int rowCount(const QModelIndex & parent = QModelIndex()) const;
  virtual int columnCount(const QModelIndex & parent = QModelIndex()) const;
  virtual bool hasChildren(const QModelIndex & parent = QModelIndex()) const;

private slots:

private:
  EkamDashboardPlugin* plugin;
  EkamTreeNode* root;

  EkamTreeNode* indexToNode(const QModelIndex& index) {
    return index.isValid() ? reinterpret_cast<EkamTreeNode*>(index.internalPointer()) : root;
  }
};

class EkamTreeWidget : public QWidget {
  Q_OBJECT
public:
  explicit EkamTreeWidget(EkamDashboardPlugin *plugin = 0);
  virtual ~EkamTreeWidget();
  
private slots:
  void addFile(ekam::proto::TaskUpdate::State state, const QString& verb, const QString& noun,
               const QString& diskPath, int firstTaskId);

private:
  EkamDashboardPlugin* plugin;

  QTreeView* view;
};

class EkamTreeWidgetFactory : public Core::INavigationWidgetFactory {
  Q_OBJECT

public:
  explicit EkamTreeWidgetFactory(EkamDashboardPlugin* plugin);
  virtual ~EkamTreeWidgetFactory();

  virtual QString displayName() const;
  virtual int priority() const;
  virtual Core::Id id() const;
  virtual Core::NavigationView createWidget();

private:
  EkamDashboardPlugin* plugin;
};

} // namespace Internal
} // namespace EkamDashboard

#endif // EKAMTREEWIDGET_H
