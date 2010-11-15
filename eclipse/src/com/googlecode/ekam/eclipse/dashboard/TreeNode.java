package com.googlecode.ekam.eclipse.dashboard;

public interface TreeNode {
  enum Type {
    DIRECTORY,

    DELETED,
    PENDING,
    RUNNING,
    DONE,
    PASSED,
    FAILED,
    BLOCKED,

    INFO,
    WARNING,
    ERROR
  }
  Type getType();

  String getLabel();

  TreeNode getParent();

  Iterable<TreeNode> getChildren();
}
