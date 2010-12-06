package com.googlecode.ekam.eclipse.dashboard;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;

import org.eclipse.core.resources.IFile;
import org.eclipse.ui.IWorkbenchPage;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.ide.IDE;

import com.googlecode.ekam.proto.DashboardProto.TaskUpdate;

public class ActionNode implements TreeNode {
  private final DirectoryNode parent;
  private final String name;

  private TaskUpdate.State state = TaskUpdate.State.DELETED;
  private String noun = null;
  private String verb = null;
  private boolean silent = false;
  private final List<LogLineNode> log = new ArrayList<LogLineNode>();
  private StringBuilder unparsedLog = new StringBuilder();

  private boolean ignoreFailure = false;

  private IFile file = null;

  public ActionNode(DirectoryNode parent, String name, TaskUpdate initialUpdate) {
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
    return silent && log.isEmpty() && state != TaskUpdate.State.FAILED;
  }

  public boolean applyUpdate(TaskUpdate update) {
    if (update.hasState()) {
      state = update.getState();
      switch (state) {
        case DELETED:
        case PENDING:
        case RUNNING:
          clearLog();
          unparsedLog = new StringBuilder();
          break;
        default:
          // nothing
          break;
      }
      parent.refreshState();
    }
    if (update.hasNoun()) {
      noun = update.getNoun();
      file = new FileFinder().find(noun);
    }
    if (update.hasVerb()) {
      verb = update.getVerb();
    }
    if (update.hasSilent()) {
      silent = update.getSilent();
    }
    if (update.hasLog()) {
      unparsedLog.append(update.getLog());
    }
    if (update.hasState()) {
      switch (state) {
        case DONE:
        case PASSED:
        case FAILED:
        case BLOCKED:
          parseLog();
          break;
        default:
          // nothing
          break;
      }
    }

    return state != TaskUpdate.State.DELETED;
  }

  private void parseLog() {
    if (unparsedLog.length() > 0) {
      FileFinder fileFinder = new FileFinder();
      for (String line : unparsedLog.toString().split("\n")) {
        log.add(new LogLineNode(this, line, log.size(), fileFinder));
      }
      unparsedLog = new StringBuilder();
    }
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
        if (ignoreFailure) {
          return Type.FAILED_IGNORED;
        } else {
          return Type.FAILED;
        }
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

  @Override
  public void openEditor(IWorkbenchPage page) throws PartInitException {
    if (file != null && file.exists()) {
      IDE.openEditor(page, file);
    }
  }

  @Override
  public void dispose() {
    clearLog();
  }

  private void clearLog() {
    for (LogLineNode logMessage : log) {
      logMessage.dispose();
    }
    log.clear();
  }

  @Override
  public boolean getIgnoreFailure() {
    return ignoreFailure;
  }

  @Override
  public void setIgnoreFailure(boolean enabled) {
    if (ignoreFailure != enabled) {
      ignoreFailure = enabled;
      parent.refreshState();
    }
  }
}
