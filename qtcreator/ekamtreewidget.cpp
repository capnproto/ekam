#include "ekamtreewidget.h"
#include "ekamdashboardplugin.h"

namespace EkamDashboard {
namespace Internal {

EkamTreeModel::EkamTreeModel(EkamDashboardPlugin* plugin, QObject* parent = 0)
  : QAbstractItemModel(parent), plugin(plugin) {}
EkamTreeModel::~EkamTreeModel() {}

QModelIndex EkamTreeModel::index(int row, int column, const QModelIndex & parent) const {

}

QModelIndex EkamTreeModel::parent(const QModelIndex &index) const {
  EkamTreeNode* node = indexToNode(index);
  if (node == root) {
    // This should never happen?
    qWarning() << "Called parent() on invisible root object?";
    return QModelIndex();
  } else {
    EkamTreeNode* parent = reinterpret_cast<EkamTreeNode*>(node->parent());

    if (parent == root) {
      return QModelIndex();
    }

    int row = parent->parent()->children().indexOf(parent);
    if (row == -1) {
      qWarning() << "obj->parent()->children() doesn't contain obj?";
      return QModelIndex();
    }

    return QModelIndex(row, 0, parent, this);
  }
}

QVariant EkamTreeModel::data(const QModelIndex &index, int role) const {
  EkamTreeNode* node = indexToNode(index);
  switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
      // string title
      return node->name();

    case Qt::ToolTipRole:
      // string tooltip
      return node->action()->getPath();

    case Qt::DecorationRole:
      // icon
      switch (node->action()->getState()) {
        // TODO
      }

    case Qt::FontRole:
      // We could bold this or something.
      return QFont();
  }

  return QVariant();
}

int EkamTreeModel::rowCount(const QModelIndex & parent = QModelIndex()) const {
  return !indexToNode(parent)->children().size();
}

int EkamTreeModel::columnCount(const QModelIndex & parent = QModelIndex()) const {
  return 1;
}

bool EkamTreeModel::hasChildren(const QModelIndex & parent = QModelIndex()) const {
  return !indexToNode(parent)->children().empty();
}

// =======================================================================================

EkamTreeWidget::EkamTreeWidget(EkamDashboardPlugin* plugin)
  : QWidget(), plugin(plugin) {

}

// =======================================================================================

explicit EkamTreeWidgetFactory::EkamTreeWidgetFactory(EkamDashboardPlugin* plugin)
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
  return Code::Id("EkamActions");
}

Core::NavigationView EkamTreeWidgetFactory::createWidget() {
  Core::NavigationView result;

  EkamTreeWidget* tree = new EkamTreeWidget(plugin);
  result.widget = tree;

  return result;
}

} // namespace Internal
} // namespace EkamDashboard
