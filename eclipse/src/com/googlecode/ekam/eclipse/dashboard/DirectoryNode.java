package com.googlecode.ekam.eclipse.dashboard;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;

import org.eclipse.ui.IWorkbenchPage;
import org.eclipse.ui.PartInitException;

import com.googlecode.ekam.proto.DashboardProto.TaskUpdate;

public class DirectoryNode implements TreeNode {
  private final DirectoryNode parent;
  private final String name;
  private final Map<String, List<ActionNode>> actions = new HashMap<String, List<ActionNode>>();
  private final Map<String, DirectoryNode> subdirs = new HashMap<String, DirectoryNode>();
  private boolean ignoreFailure = false;
  private Type type = Type.DIRECTORY;

  public DirectoryNode(DirectoryNode parent, String name) {
    this.parent = parent;
    this.name = name;
  }

  public DirectoryNode() {
    this.parent = null;
    this.name = "";
  }

  public void clear() {
    disposeChildren();
    actions.clear();
    subdirs.clear();
    if (type != Type.DIRECTORY) {
      type = Type.DIRECTORY;
      if (parent != null) {
        parent.refreshState();
      }
    }
  }

  public ActionNode newAction(String path, TaskUpdate initialUpdate) {
    String[] parts = path.split("/", 2);

    if (parts.length == 1) {
      List<ActionNode> list = actions.get(parts[0]);
      if (list == null) {
        list = new ArrayList<ActionNode>();
        actions.put(parts[0], list);
      } else {
        for (ActionNode action : list) {
          if (action.tryReuse(initialUpdate)) {
            return action;
          }
        }
      }

      ActionNode action = new ActionNode(this, parts[0], initialUpdate);
      list.add(action);
      return action;
    } else {
      DirectoryNode subdir = subdirs.get(parts[0]);
      if (subdir == null) {
        subdir = new DirectoryNode(this, parts[0]);
        subdirs.put(parts[0], subdir);
      }
      return subdir.newAction(parts[1], initialUpdate);
    }
  }

  @Override
  public Type getType() {
    return type;
  }

  @Override
  public String getLabel() {
    return name;
  }

  @Override
  public TreeNode getParent() {
    return parent;
  }

  @Override
  public Iterable<TreeNode> getChildren() {
    return new Iterable<TreeNode>() {
      @Override
      public Iterator<TreeNode> iterator() {
        return new Iterator<TreeNode>() {
          Iterator<List<ActionNode>> actionGroupIter = actions.values().iterator();
          Iterator<ActionNode> actionIter = Collections.<ActionNode>emptyList().iterator();
          ActionNode nextAction = findNextVisibleAction();
          Iterator<DirectoryNode> subdirIter = subdirs.values().iterator();

          private ActionNode findNextVisibleAction() {
            while (true) {
              while (!actionIter.hasNext()) {
                if (!actionGroupIter.hasNext()) {
                  return null;
                }
                actionIter = actionGroupIter.next().iterator();
              }
              ActionNode action = actionIter.next();
              if (!action.isSilent()) {
                return action;
              }
            }
          }

          @Override
          public boolean hasNext() {
            return nextAction != null || subdirIter.hasNext();
          }

          @Override
          public TreeNode next() {
            if (nextAction == null) {
              return subdirIter.next();
            } else {
              TreeNode result = nextAction;
              nextAction = findNextVisibleAction();
              return result;
            }
          }

          @Override
          public void remove() {
            throw new UnsupportedOperationException("Cannot remove.");
          }
        };
      }
    };
  }

  @Override
  public void openEditor(IWorkbenchPage page) throws PartInitException {
    // Do nothing.
  }

  @Override
  public void dispose() {
    disposeChildren();
  }

  private void disposeChildren() {
    for (DirectoryNode subdir : subdirs.values()) {
      subdir.dispose();
    }
    for (List<ActionNode> actionList : actions.values()) {
      for (ActionNode action : actionList) {
        action.dispose();
      }
    }
  }

  public void refreshState() {
    Type newType;
    if (checkChildrenForRunning()) {
      newType = Type.DIRECTORY_RUNNING;
    } else if (checkChildrenForErrors()) {
      newType = ignoreFailure ? Type.DIRECTORY_WITH_ERRORS_IGNORED : Type.DIRECTORY_WITH_ERRORS;
    } else {
      newType = Type.DIRECTORY;
    }

    if (type  != newType) {
      type = newType;
      if (parent != null) {
        parent.refreshState();
      }
    }
  }

  private boolean checkChildrenForRunning() {
    for (DirectoryNode subdir : subdirs.values()) {
      if (subdir.getType() == Type.DIRECTORY_RUNNING) {
        return true;
      }
    }
    for (List<ActionNode> actionList : actions.values()) {
      for (ActionNode action : actionList) {
        if (action.getType() == Type.RUNNING) {
          return true;
        }
      }
    }
    return false;
  }

  private boolean checkChildrenForErrors() {
    for (DirectoryNode subdir : subdirs.values()) {
      if (subdir.getType() == Type.DIRECTORY_WITH_ERRORS) {
        return true;
      }
    }
    for (List<ActionNode> actionList : actions.values()) {
      for (ActionNode action : actionList) {
        if (action.getType() == Type.FAILED) {
          return true;
        }
      }
    }
    return false;
  }

  @Override
  public boolean getIgnoreFailure() {
    return ignoreFailure;
  }

  @Override
  public void setIgnoreFailure(boolean enabled) {
    if (ignoreFailure != enabled) {
      ignoreFailure = enabled;
      if (parent != null) {
        parent.refreshState();
      }
    }
  }
}
