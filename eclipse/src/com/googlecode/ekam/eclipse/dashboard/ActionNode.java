package com.googlecode.ekam.eclipse.dashboard;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;

import com.googlecode.ekam.proto.DashboardProto.TaskUpdate;

public class ActionNode implements TreeNode {
  private final TreeNode parent;
  private final String name;

  private TaskUpdate.State state = TaskUpdate.State.DELETED;
  private String noun = null;
  private String verb = null;
  private boolean silent = false;
  private final List<LogLineNode> log = new ArrayList<LogLineNode>();

  public ActionNode(TreeNode parent, String name, TaskUpdate initialUpdate) {
    this.parent = parent;
    this.name = name;
    applyUpdate(initialUpdate);
  }

  public boolean tryReuse(TaskUpdate initialUpdate) {
    if (state == TaskUpdate.State.DELETED && initialUpdate.getVerb().equals(verb)) {
      silent = false;
      applyUpdate(initialUpdate);
      return true;
    } else {
      return false;
    }
  }

  public TaskUpdate.State getState() {
    return state;
  }

  public String getNoun() {
    return noun;
  }

  public boolean isSilent() {
    return silent && log.isEmpty();
  }

  public boolean applyUpdate(TaskUpdate update) {
    if (update.hasState()) {
      state = update.getState();
      switch (state) {
        case DELETED:
        case PENDING:
        case RUNNING:
          log.clear();
          break;
        default:
          // nothing
          break;
      }
    }
    if (update.hasNoun()) {
      noun = update.getNoun();
    }
    if (update.hasVerb()) {
      verb = update.getVerb();
    }
    if (update.hasSilent()) {
      silent = update.getSilent();
    }
    if (update.hasLog()) {
      for (String line : update.getLog().split("\n")) {
        log.add(new LogLineNode(this, line, log.size()));
      }
    }

    return state != TaskUpdate.State.DELETED;
  }

  @Override
  public Type getType() {
    switch (state) {
      case DELETED:
        return Type.DELETED;
      case PENDING:
        return Type.PENDING;
      case RUNNING:
        return Type.RUNNING;
      case DONE:
        return Type.DONE;
      case PASSED:
        return Type.PASSED;
      case FAILED:
        return Type.FAILED;
      case BLOCKED:
        return Type.BLOCKED;
    }
    return Type.DELETED;
  }

  @Override
  public String getLabel() {
    if (verb == null) {
      return name;
    } else {
      return verb + ": " + name;
    }
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
          Iterator<LogLineNode> inner = log.iterator();

          @Override
          public boolean hasNext() {
            return inner.hasNext();
          }

          @Override
          public TreeNode next() {
            return inner.next();
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
