package com.googlecode.ekam.eclipse.dashboard;
import java.util.HashMap;
import java.util.Map;

import org.eclipse.core.resources.IFile;
import org.eclipse.core.resources.IProject;
import org.eclipse.core.resources.IWorkspaceRoot;
import org.eclipse.core.resources.ResourcesPlugin;
import org.eclipse.core.runtime.IPath;
import org.eclipse.core.runtime.Path;

class FileFinder {
  private final IWorkspaceRoot root = ResourcesPlugin.getWorkspace().getRoot();
  private final Map<String, IFile> cache = new HashMap<String, IFile>();

  public FileFinder() {}

  public IFile find(String pathString) {
    if (cache.containsKey(pathString)) {
      return cache.get(pathString);
    }

    IFile file = findUncached(new Path(pathString));
    cache.put(pathString, file);
    return file;
  }

  private final IPath SRC = new Path("src");
  private final IPath TMP = new Path("tmp");

  private IFile findUncached(IPath path) {
    IPath srcPath = SRC.append(path);
    IPath tmpPath = TMP.append(path);

    if (path.segmentCount() > 1) {
      IFile file = root.getFile(path);
      if (file.exists()) {
        return file;
      }
    }

    {
      IFile file = root.getFile(srcPath);
      if (file.exists()) {
        return file;
      }
    }

    {
      IFile file = root.getFile(tmpPath);
      if (file.exists()) {
        return file;
      }
    }

    for (IProject project : root.getProjects()) {
      IFile file = project.getFile(path);
      if (file.exists()) {
        return file;
      }
    }

    for (IProject project : root.getProjects()) {
      IFile file = project.getFile(srcPath);
      if (file.exists()) {
        return file;
      }
    }

    for (IProject project : root.getProjects()) {
      IFile file = project.getFile(tmpPath);
      if (file.exists()) {
        return file;
      }
    }

    return null;
  }
}
