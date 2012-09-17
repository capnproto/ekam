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

#ifndef EKAMTREEWIDGET_H
#define EKAMTREEWIDGET_H

#include <coreplugin/inavigationwidgetfactory.h>

#include <QWidget>
#include <QAbstractItemModel>
#include <QTreeView>

#include <projectexplorer/task.h>

#include "dashboard.pb.h"

namespace EkamDashboard {
namespace Internal {

class EkamDashboardPlugin;
class EkamTreeModel;
class ActionState;

class EkamTreeNode : public QObject {
  Q_OBJECT
public:
  explicit EkamTreeNode(EkamTreeModel* tree);
  EkamTreeNode(EkamTreeNode* parent, const QString& name, bool isDirectory);
  virtual ~EkamTreeNode();

  int row();
  QModelIndex index();

  void createNode(const QString& noun, const QString& verb, ActionState* action);

  QVariant data(int role);

  int childCount() {
    return childNodes.size();
  }
  EkamTreeNode* getChild(int index) {
    return childNodes.at(index);
  }

  ActionState* getAction() {
    return action;
  }

private slots:
  void actionStateChanged(ekam::proto::TaskUpdate::State newState);
  void actionRemoved();

private:
  EkamTreeModel* tree;

  bool isDirectory;
  QString name;

  EkamTreeNode* parentNode;
  QList<EkamTreeNode*> childNodes;

  ActionState* action;
  int state;

  void setAction(ActionState* newAction);
  void stateChanged(int newState);
  void childStateChanged();
  void removeChild(EkamTreeNode* child);
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

  EkamTreeNode* indexToNode(const QModelIndex& index) const {
    return index.isValid() ? reinterpret_cast<EkamTreeNode*>(index.internalPointer()) : root;
  }

private slots:
  void newAction(ActionState* action);

private:
  friend class EkamTreeNode;

  EkamDashboardPlugin* plugin;
  EkamTreeNode* root;
};

class EkamTreeWidget : public QWidget {
  Q_OBJECT
public:
  explicit EkamTreeWidget(EkamDashboardPlugin *plugin = 0);
  virtual ~EkamTreeWidget();

private slots:
  void jumpTo(const QModelIndex& index);

private:
  EkamDashboardPlugin* plugin;

  EkamTreeModel* model;
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
