package com.googlecode.ekam.eclipse.dashboard;

import java.util.Collections;

import org.eclipse.core.resources.IFile;
import org.eclipse.core.resources.IMarker;
import org.eclipse.core.runtime.CoreException;
import org.eclipse.ui.IWorkbenchPage;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.ide.IDE;

public class LogLineNode implements TreeNode {
  private final TreeNode parent;
  private final ParsedLogLine line;
  private final int index;
  private final IMarker marker;

  public LogLineNode(TreeNode parent, String lineText, int index, FileFinder fileFinder) {
    this.parent = parent;
    this.line = new ParsedLogLine(lineText);
    this.index = index;

    IMarker marker = null;

    if (line.filename != null) {
      IFile file = fileFinder.find(line.filename);
      if (file != null) {
        try {
          marker = file.createMarker("com.googlecode.ekam.eclipse.dashboard.ekamProblem");
          marker.setAttribute(IMarker.MESSAGE, line.message);
          marker.setAttribute(IMarker.SEVERITY, getMarkerSeverity());
          if (line.locationLine >= 0) {
            marker.setAttribute(IMarker.LINE_NUMBER, line.locationLine);
          }
        } catch (CoreException e) {
          e.printStackTrace();
        }
      }
    }

    this.marker = marker;
  }

  private int getMarkerSeverity() {
    switch (line.type) {
      case ERROR:
        return IMarker.SEVERITY_ERROR;
      case WARNING:
        return IMarker.SEVERITY_WARNING;
      default:
        return IMarker.SEVERITY_INFO;
    }
  }

  public int getIndex() {
    return index;
  }

  @Override
  public Type getType() {
    switch (line.type) {
      case ERROR:
        return Type.ERROR;
      case WARNING:
        return Type.WARNING;
      default:
        return Type.INFO;
    }
  }

  @Override
  public String getLabel() {
    return line.fullText;
  }

  @Override
  public TreeNode getParent() {
    return parent;
  }

  @Override
  public Iterable<TreeNode> getChildren() {
    return Collections.emptyList();
  }

  @Override
  public void openEditor(IWorkbenchPage page) throws PartInitException {
    if (marker != null && marker.exists()) {
      IDE.openEditor(page, marker);
    }
  }

  @Override
  public void dispose() {
    if (marker != null) {
      try {
        marker.delete();
      } catch (CoreException e) {
        e.printStackTrace();
      }
    }
  }

  @Override
  public boolean getIgnoreFailure() {
    return false;
  }

  @Override
  public void setIgnoreFailure(boolean enabled) {
    // irrelevant
  }
}
