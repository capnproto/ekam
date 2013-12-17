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

#include "ekamtreewidget.h"
#include "ekamdashboardplugin.h"

#include <QVBoxLayout>

#include <coreplugin/editormanager/editormanager.h>
#include <projectexplorer/taskhub.h>
#include <utils/navigationtreeview.h>

namespace EkamDashboard {
namespace Internal {

static const int DEFAULT_STATE = ekam::proto::TaskUpdate::State_MAX + 1;

// This defines the priority ordering of states.  The state (and icon) for a directory will be
// chosen based on the highest-priority state of its children.
static const int ORDERED_STATES[] = {
  DEFAULT_STATE,
  ekam::proto::TaskUpdate::DELETED,
  ekam::proto::TaskUpdate::DONE,
  ekam::proto::TaskUpdate::PASSED,
  ekam::proto::TaskUpdate::FAILED,
  ekam::proto::TaskUpdate::PENDING,
  ekam::proto::TaskUpdate::BLOCKED,
  ekam::proto::TaskUpdate::RUNNING,
};

static int STATE_PRIORITIES[DEFAULT_STATE + 1];

struct StatePrioritiesInitializer {
  StatePrioritiesInitializer() {
    for (size_t i = 0; i < (sizeof(STATE_PRIORITIES) / sizeof(STATE_PRIORITIES[0])); i++) {
      STATE_PRIORITIES[i] = -1;
    }
    for (size_t i = 0; i < (sizeof(ORDERED_STATES) / sizeof(ORDERED_STATES[0])); i++) {
      STATE_PRIORITIES[ORDERED_STATES[i]] = i;
    }
  }
};
static StatePrioritiesInitializer statePrioritiesInitializer;

// =======================================================================================

EkamTreeNode::EkamTreeNode(EkamTreeModel* tree)
  : QObject(tree), tree(tree), isDirectory(true), parentNode(0), action(0), state(DEFAULT_STATE) {}
EkamTreeNode::EkamTreeNode(EkamTreeNode* parent, const QString& name, bool isDirectory)
  : QObject(parent), tree(parent->tree), isDirectory(isDirectory), name(name),
    parentNode(parent), action(0), state(DEFAULT_STATE) {}
EkamTreeNode::~EkamTreeNode() {}

int EkamTreeNode::row() {
  if (this == tree->root) {
    return -1;
  }

  int result = parentNode->childNodes.indexOf(this);
  if (result == -1) {
    qWarning() << "parentNode->childNodes doesn't contain this?";
  }
  return result;
}

QModelIndex EkamTreeNode::index() {
  if (this == tree->root) {
    return QModelIndex();
  }

  int rowNum = row();
  if (rowNum == -1) {
    return QModelIndex();
  }

  return tree->createIndex(rowNum, 0, this);
}

void EkamTreeNode::actionStateChanged(ekam::proto::TaskUpdate::State newState) {
  stateChanged(newState);
}

void EkamTreeNode::actionRemoved() {
  setAction(0);
  if (parentNode != 0) {
    parentNode->removeChild(this);
  }
//  QModelIndex myIndex = index();
//  emit tree->dataChanged(myIndex, myIndex);
}

void EkamTreeNode::createNode(const QString& noun, const QString& verb, ActionState* action) {
  int slash = noun.indexOf(QLatin1Char('/'));

  QString childName = (slash == -1) ? QString(QLatin1String("%1 (%2)")).arg(noun, verb) : noun.mid(0, slash);

  QList<EkamTreeNode*>::iterator iter = childNodes.begin();
  while (iter < childNodes.end() && (*iter)->name < childName) {
    ++iter;
  }

  EkamTreeNode* selectedNode;
  bool isNew = iter == childNodes.end() || (*iter)->name != childName;

  if (isNew) {
    QModelIndex i = index();
    int r = iter - childNodes.begin();
    tree->beginInsertRows(i, r, r);
    selectedNode = new EkamTreeNode(this, childName, slash != -1);
    childNodes.insert(iter, selectedNode);
  } else {
    selectedNode = *iter;
  }

  if (slash == -1) {
    selectedNode->setAction(action);
  } else {
    selectedNode->createNode(noun.right(noun.size() - slash - 1), verb, action);
  }

  if (isNew) {
    tree->endInsertRows();
  } else {
    QModelIndex nodeIndex = selectedNode->index();
    emit tree->dataChanged(nodeIndex, nodeIndex);
  }
}

QVariant EkamTreeNode::data(int role) {
  switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
      // string title
      return name;

    case Qt::ToolTipRole:
      // string tooltip
      if (action == 0) {
        return QVariant();
      } else {
        action->getPath();
      }

    case Qt::DecorationRole:
      // icon
      if (isDirectory) {
        switch (state) {
          case ekam::proto::TaskUpdate::DELETED:
            return QIcon(QLatin1String(":/ekamdashboard/images/dir-deleted.png"));
          case ekam::proto::TaskUpdate::PENDING:
            return QIcon(QLatin1String(":/ekamdashboard/images/dir-pending.png"));
          case ekam::proto::TaskUpdate::RUNNING:
            return QIcon(QLatin1String(":/ekamdashboard/images/dir-running.png"));
          case ekam::proto::TaskUpdate::DONE:
            return QIcon(QLatin1String(":/ekamdashboard/images/dir-done.png"));
          case ekam::proto::TaskUpdate::PASSED:
            return QIcon(QLatin1String(":/ekamdashboard/images/dir-passed.png"));
          case ekam::proto::TaskUpdate::FAILED:
            return QIcon(QLatin1String(":/ekamdashboard/images/dir-failed.png"));
          case ekam::proto::TaskUpdate::BLOCKED:
            // Use pending icon for blocked.
            return QIcon(QLatin1String(":/ekamdashboard/images/dir-pending.png"));
          case DEFAULT_STATE:
            return QIcon(QLatin1String(":/ekamdashboard/images/dir.png"));
        }
      } else {
        switch (state) {
          case DEFAULT_STATE:
          case ekam::proto::TaskUpdate::DELETED:
            return QIcon(QLatin1String(":/ekamdashboard/images/state-deleted.png"));
          case ekam::proto::TaskUpdate::PENDING:
            return QIcon(QLatin1String(":/ekamdashboard/images/state-pending.png"));
          case ekam::proto::TaskUpdate::RUNNING:
            return QIcon(QLatin1String(":/ekamdashboard/images/state-running.png"));
          case ekam::proto::TaskUpdate::DONE:
            return QIcon(QLatin1String(":/ekamdashboard/images/state-done.png"));
          case ekam::proto::TaskUpdate::PASSED:
            return QIcon(QLatin1String(":/ekamdashboard/images/state-passed.png"));
          case ekam::proto::TaskUpdate::FAILED:
            return QIcon(QLatin1String(":/ekamdashboard/images/state-failed.png"));
          case ekam::proto::TaskUpdate::BLOCKED:
            // Use pending icon for blocked.
            return QIcon(QLatin1String(":/ekamdashboard/images/state-pending.png"));
        }
      }
      qWarning() << "Can't get here.";
      break;

    case Qt::FontRole:
      // We could bold this or something.
      QFont result;
      if (isDirectory) {
        result.setBold(true);
      }
      return result;
  }

  return QVariant();
}

void EkamTreeNode::setAction(ActionState* newAction) {
  if (action != 0) {
    disconnect(action, SIGNAL(stateChanged(ekam::proto::TaskUpdate::State)),
               this, SLOT(actionStateChanged(ekam::proto::TaskUpdate::State)));
    disconnect(action, SIGNAL(removed()), this, SLOT(actionRemoved()));
  }

  action = newAction;

  if (action != 0) {
    connect(action, SIGNAL(stateChanged(ekam::proto::TaskUpdate::State)),
            this, SLOT(actionStateChanged(ekam::proto::TaskUpdate::State)));
    connect(action, SIGNAL(removed()), this, SLOT(actionRemoved()));
  }
}

void EkamTreeNode::stateChanged(int newState) {
  if (state != newState) {
    state = newState;
    QModelIndex myIndex = index();
    emit tree->dataChanged(myIndex, myIndex);
    if (parentNode != 0) {
      parentNode->childStateChanged();
    }
  }
}

void EkamTreeNode::childStateChanged() {
  int maxState = DEFAULT_STATE;
  int maxStatePriority = STATE_PRIORITIES[maxState];

  foreach (EkamTreeNode* child, childNodes) {
    int childPriority = STATE_PRIORITIES[child->state];
    if (childPriority > maxStatePriority) {
      maxState = child->state;
      maxStatePriority = childPriority;
    }
  }

  stateChanged(maxState);
}

void EkamTreeNode::removeChild(EkamTreeNode* child) {
  int r = child->row();
  tree->beginRemoveRows(index(), r, r);
  childNodes.erase(childNodes.begin() + r);
  tree->endRemoveRows();
  if (childNodes.empty() && parentNode != 0) {
    parentNode->removeChild(this);
  }
}

// =======================================================================================

EkamTreeModel::EkamTreeModel(EkamDashboardPlugin* plugin, QObject* parent)
  : QAbstractItemModel(parent), plugin(plugin), root(new EkamTreeNode(this)) {
//  qDebug() << "EkamTreeModel::EkamTreeModel(...)";

  connect(plugin, SIGNAL(newAction(ActionState*)), this, SLOT(newAction(ActionState*)));

  foreach (ActionState* action, plugin->allActions()) {
    newAction(action);
  }
}

EkamTreeModel::~EkamTreeModel() {}

QModelIndex EkamTreeModel::index(int row, int column, const QModelIndex & parent) const {
//  qDebug() << "EkamTreeModel::index(" << row << ", " << column << ", " << parent << ")";

  if (column != 0) {
    // Invalid.
    return QModelIndex();
  }

  EkamTreeNode* parentNode = indexToNode(parent);
  if (row < 0 || row >= parentNode->childCount()) {
    // Out of bounds.
    return QModelIndex();
  }

  return createIndex(row, 0, parentNode->getChild(row));
}

QModelIndex EkamTreeModel::parent(const QModelIndex &index) const {
//  qDebug() << "EkamTreeModel::parent(" << index << ")";

  EkamTreeNode* node = indexToNode(index);
  if (node == root) {
    // This should never happen?
    qWarning() << "Called parent() on invisible root object?";
    return QModelIndex();
  } else {
    return reinterpret_cast<EkamTreeNode*>(node->parent())->index();
  }
}

QVariant EkamTreeModel::data(const QModelIndex &index, int role) const {
//  qDebug() << "EkamTreeModel::data(" << index << ", " << role << ") = "
//           << indexToNode(index)->data(role);
  return indexToNode(index)->data(role);
}

int EkamTreeModel::rowCount(const QModelIndex & parent) const {
//  qDebug() << "EkamTreeModel::rowCount(" << parent << ") = "
//           << indexToNode(parent)->childCount();
  return indexToNode(parent)->childCount();
}

int EkamTreeModel::columnCount(const QModelIndex & parent) const {
//  qDebug() << "EkamTreeModel::columnCount(" << parent << ") = 1";
  Q_UNUSED(parent);
  return 1;
}

bool EkamTreeModel::hasChildren(const QModelIndex & parent) const {
//  qDebug() << "EkamTreeModel::hasChildren(" << parent << ") = "
//           << (indexToNode(parent)->childCount() > 0);
  return indexToNode(parent)->childCount() > 0;
}

void EkamTreeModel::newAction(ActionState* action) {
//  qDebug() << "EkamTreeModel::newAction(" << action->getVerb() << ":" << action->getNoun() << ")";
  root->createNode(action->getNoun(), action->getVerb(), action);
}

// =======================================================================================

EkamTreeWidget::EkamTreeWidget(EkamDashboardPlugin* plugin)
  : QWidget(), plugin(plugin) {
  model = new EkamTreeModel(plugin, this);

  view = new Utils::NavigationTreeView(this);
  view->setModel(model);
  setFocusProxy(view);

  QVBoxLayout *layout = new QVBoxLayout();
  layout->addWidget(view);
  layout->setContentsMargins(0, 0, 0, 0);
  setLayout(layout);

  connect(view, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(jumpTo(QModelIndex)));
}

EkamTreeWidget::~EkamTreeWidget() {}

void EkamTreeWidget::jumpTo(const QModelIndex& index) {
  ActionState* action = model->indexToNode(index)->getAction();
  if (action != 0) {
    const ProjectExplorer::Task* task = action->firstTask();
    if (task == 0) {
      // Open in editor.
      Core::EditorManager* editorManager = Core::EditorManager::instance();
      editorManager->openEditor(action->getPath(), Core::Id());
    } else {
      plugin->taskHub()->taskMarkClicked(task->taskId);
    }
  }
}

// =======================================================================================

EkamTreeWidgetFactory::EkamTreeWidgetFactory(EkamDashboardPlugin* plugin)
  : INavigationWidgetFactory(), plugin(plugin) {}
EkamTreeWidgetFactory::~EkamTreeWidgetFactory() {}

QString EkamTreeWidgetFactory::displayName() const {
  return QLatin1String("Ekam Actions");
}

int EkamTreeWidgetFactory::priority() const {
  // No idea what this means.
  return 100;
}

Core::Id EkamTreeWidgetFactory::id() const {
  return Core::Id("EkamActions");
}

Core::NavigationView EkamTreeWidgetFactory::createWidget() {
  Core::NavigationView result;

  EkamTreeWidget* tree = new EkamTreeWidget(plugin);
  result.widget = tree;

  return result;
}

} // namespace Internal
} // namespace EkamDashboard
