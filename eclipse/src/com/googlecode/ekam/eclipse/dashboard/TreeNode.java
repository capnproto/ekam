package com.googlecode.ekam.eclipse.dashboard;

import org.eclipse.ui.IWorkbenchPage;
import org.eclipse.ui.PartInitException;

public interface TreeNode {
  enum Type {
    DIRECTORY,
    DIRECTORY_RUNNING,
    DIRECTORY_WITH_ERRORS,
    DIRECTORY_WITH_ERRORS_IGNORED,

    DELETED,
    PENDING,
    RUNNING,
    DONE,
    PASSED,
    FAILED,
    FAILED_IGNORED,
    BLOCKED,

    INFO,
    WARNING,
    ERROR
  }
  Type getType();

  String getLabel();

  TreeNode getParent();

  Iterable<TreeNode> getChildren();

  void openEditor(IWorkbenchPage page) throws PartInitException;

  void dispose();

  boolean getIgnoreFailure();
  void setIgnoreFailure(boolean enabled);
}
