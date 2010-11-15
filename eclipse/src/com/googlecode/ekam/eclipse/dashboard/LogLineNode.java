package com.googlecode.ekam.eclipse.dashboard;

import java.util.Collections;

public class LogLineNode implements TreeNode {
  private final TreeNode parent;
  private final String line;
  private final int index;

  public LogLineNode(TreeNode parent, String line, int index) {
    this.parent = parent;
    this.line = line;
    this.index = index;
  }

  public int getIndex() {
    return index;
  }

  @Override
  public Type getType() {
    return Type.ERROR;
  }

  @Override
  public String getLabel() {
    return line;
  }

  @Override
  public TreeNode getParent() {
    return parent;
  }

  @Override
  public Iterable<TreeNode> getChildren() {
    return Collections.emptyList();
  }
}
