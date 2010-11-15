package com.googlecode.ekam.eclipse.dashboard;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;

import com.googlecode.ekam.proto.DashboardProto.TaskUpdate;

public class DirectoryNode implements TreeNode {
  private final TreeNode parent;
  private final String name;
  private final Map<String, List<ActionNode>> actions = new HashMap<String, List<ActionNode>>();
  private final Map<String, DirectoryNode> subdirs = new HashMap<String, DirectoryNode>();

  public DirectoryNode(TreeNode parent, String name) {
    this.parent = parent;
    this.name = name;
  }

  public DirectoryNode() {
    this.parent = null;
    this.name = "";
  }

  public void clear() {
    actions.clear();
    subdirs.clear();
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
    return Type.DIRECTORY;
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
}
