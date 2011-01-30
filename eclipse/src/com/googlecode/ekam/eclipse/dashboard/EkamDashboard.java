package com.googlecode.ekam.eclipse.dashboard;

import java.util.ArrayList;
import java.util.List;

import org.eclipse.core.resources.IMarker;
import org.eclipse.core.resources.IProject;
import org.eclipse.core.resources.IResource;
import org.eclipse.core.resources.ResourcesPlugin;
import org.eclipse.core.runtime.CoreException;
import org.eclipse.jface.action.Action;
import org.eclipse.jface.action.IMenuListener;
import org.eclipse.jface.action.IMenuManager;
import org.eclipse.jface.action.IToolBarManager;
import org.eclipse.jface.action.MenuManager;
import org.eclipse.jface.action.Separator;
import org.eclipse.jface.dialogs.MessageDialog;
import org.eclipse.jface.viewers.DoubleClickEvent;
import org.eclipse.jface.viewers.IDoubleClickListener;
import org.eclipse.jface.viewers.ILabelProvider;
import org.eclipse.jface.viewers.ILabelProviderListener;
import org.eclipse.jface.viewers.ISelection;
import org.eclipse.jface.viewers.IStructuredSelection;
import org.eclipse.jface.viewers.ITreeContentProvider;
import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.jface.viewers.Viewer;
import org.eclipse.jface.viewers.ViewerComparator;
import org.eclipse.swt.SWT;
import org.eclipse.swt.SWTException;
import org.eclipse.swt.graphics.Image;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.ui.IActionBars;
import org.eclipse.ui.ISharedImages;
import org.eclipse.ui.IWorkbenchActionConstants;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.part.DrillDownAdapter;
import org.eclipse.ui.part.ViewPart;

import com.googlecode.ekam.eclipse.dashboard.TreeNode.Type;


/**
 * This sample class demonstrates how to plug-in a new
 * workbench view. The view shows data obtained from the
 * model. The sample creates a dummy model on the fly,
 * but a real implementation would connect to the model
 * available either in this or another plug-in (e.g. the workspace).
 * The view is connected to the model using a content provider.
 * <p>
 * The view uses a label provider to define how model
 * objects should be presented in the view. Each
 * view can present the same model objects using
 * different labels and icons, if needed. Alternatively,
 * a single label provider can be shared between views
 * in order to ensure that objects of the same type are
 * presented in the same way everywhere.
 * <p>
 */

public class EkamDashboard extends ViewPart {

  /**
   * The ID of the view as specified by the extension.
   */
  public static final String ID = "com.googlecode.ekam.eclipse.dashboard.EkamDashboard";

  private TreeViewer viewer;
  private DrillDownAdapter drillDownAdapter;
  private Action action1;
  private Action action2;
  private Action doubleClickAction;
  private Thread eventThread;

  private DirectoryNode root;

  class ViewContentProvider implements ITreeContentProvider {
    @Override
    public void inputChanged(Viewer v, Object oldInput, Object newInput) {
    }
    @Override
    public void dispose() {
    }
    @Override
    public Object[] getElements(Object inputElement) {
      return getChildren(inputElement);
    }
    @Override
    public Object getParent(Object child) {
      return ((TreeNode) child).getParent();
    }
    @Override
    public Object [] getChildren(Object parent) {
      Iterable<TreeNode> children = ((TreeNode) parent).getChildren();
      List<TreeNode> list = new ArrayList<TreeNode>();
      for (TreeNode child : children) {
        list.add(child);
      }
      return list.toArray();
    }
    @Override
    public boolean hasChildren(Object parent) {
      return ((TreeNode) parent).getChildren().iterator().hasNext();
    }
  }
  class ViewLabelProvider implements ILabelProvider {
    @Override
    public String getText(Object obj) {
      return ((TreeNode) obj).getLabel();
    }
    @Override
    public Image getImage(Object obj) {
      Image image = getSharedImage(ISharedImages.IMG_OBJ_ELEMENT);

      Type type = ((TreeNode) obj).getType();
      switch (type) {
        case DIRECTORY:
          image = getSharedImage(ISharedImages.IMG_OBJ_FOLDER);
          break;
        case DIRECTORY_RUNNING:
          image = Activator.DIRECTORY_RUNNING_IMG;
          break;
        case DIRECTORY_WITH_ERRORS:
          image = Activator.DIRECTORY_WITH_ERRORS_IMG;
          break;
        case DIRECTORY_WITH_ERRORS_IGNORED:
          image = Activator.DIRECTORY_WITH_ERRORS_IGNORED_IMG;
          break;

        case DELETED:
          image = Activator.DELETED_IMG;
          break;
        case PENDING:
          image = Activator.PENDING_IMG;
          break;
        case RUNNING:
          image = Activator.RUNNING_IMG;
          break;
        case DONE:
          image = Activator.DONE_IMG;
          break;
        case PASSED:
          image = Activator.PASSED_IMG;
          break;
        case FAILED:
          image = Activator.FAILED_IMG;
          break;
        case FAILED_IGNORED:
          image = Activator.FAILED_IGNORED_IMG;
          break;
        case BLOCKED:
          image = Activator.BLOCKED_IMG;
          break;

        case INFO:
          image = getSharedImage(ISharedImages.IMG_OBJS_INFO_TSK);
          break;
        case WARNING:
          image = getSharedImage(ISharedImages.IMG_OBJS_WARN_TSK);
          break;
        case ERROR:
          image = getSharedImage(ISharedImages.IMG_OBJS_ERROR_TSK);
          break;
      }

      return image;
    }
    private Image getSharedImage(String imageKey) {
      return PlatformUI.getWorkbench().getSharedImages().getImage(imageKey);
    }

    @Override
    public void addListener(ILabelProviderListener listener) {
      // Label never changes, so ignore.
    }
    @Override
    public void dispose() {
      // Nothing to do here.
    }
    @Override
    public boolean isLabelProperty(Object element, String property) {
      // Labels currently do not change.
      return false;
    }
    @Override
    public void removeListener(ILabelProviderListener listener) {
      // Label never changes, so ignore.
    }
  }

  class NodeComparator extends ViewerComparator {
    @Override
    public int category(Object element) {
      return element instanceof DirectoryNode ? 0 : 1;
    }

    @Override
    public int compare(Viewer viewer, Object e1, Object e2) {
      if (e1 instanceof LogLineNode && e2 instanceof LogLineNode) {
        return ((LogLineNode) e1).getIndex() - ((LogLineNode) e2).getIndex();
      } else {
        return super.compare(viewer, e1, e2);
      }
    }
  }

  /**
   * The constructor.
   */
  public EkamDashboard() {
  }

  @Override
  public void dispose() {
    if (eventThread != null) {
      eventThread.interrupt();
    }
    if (root != null) {
      root.dispose();
      root = null;
    }
    super.dispose();
  }

  /**
   * This is a callback that will allow us
   * to create the viewer and initialize it.
   */
  @Override
  public void createPartControl(Composite parent) {
    deleteOldMarkers();

    viewer = new TreeViewer(parent, SWT.MULTI | SWT.H_SCROLL | SWT.V_SCROLL);
    drillDownAdapter = new DrillDownAdapter(viewer);
    viewer.setContentProvider(new ViewContentProvider());
    viewer.setLabelProvider(new ViewLabelProvider());
    viewer.setComparator(new NodeComparator());

    root = new DirectoryNode();
    viewer.setInput(root);

    connectToEkam(root);

    // Create the help context id for the viewer's control
//    PlatformUI.getWorkbench().getHelpSystem().setHelp(viewer.getControl(), "ekam-dashboard.viewer");
    makeActions();
    hookContextMenu();
    hookDoubleClickAction();
    contributeToActionBars();
  }

  private void deleteOldMarkers() {
    // TODO Auto-generated method stub
    for (IProject project : ResourcesPlugin.getWorkspace().getRoot().getProjects()) {
      try {
        IMarker[] markers =
            project.findMarkers("com.googlecode.ekam.eclipse.dashboard.ekamProblem",
                                true, IResource.DEPTH_INFINITE);
        for (IMarker marker : markers) {
          marker.delete();
        }
      } catch (CoreException e) {
        // TODO Auto-generated catch block
        e.printStackTrace();
      }
    }
  }

  private void connectToEkam(DirectoryNode root) {
    if (eventThread != null) {
      eventThread.interrupt();
    }

    Runnable onChange = new Runnable() {
      @Override
      public void run() {
        try {
          viewer.refresh();
        } catch (SWTException e) {
          // The viewer was probably disposed already.
        }
      }
    };
    eventThread = new Thread(new EventThread(root, onChange));
    eventThread.start();
  }

  private void hookContextMenu() {
    MenuManager menuMgr = new MenuManager("#PopupMenu");
    menuMgr.setRemoveAllWhenShown(true);
    menuMgr.addMenuListener(new IMenuListener() {
      @Override
      public void menuAboutToShow(IMenuManager manager) {
        EkamDashboard.this.fillContextMenu(manager);
      }
    });
    Menu menu = menuMgr.createContextMenu(viewer.getControl());
    viewer.getControl().setMenu(menu);
    getSite().registerContextMenu(menuMgr, viewer);
  }

  private void contributeToActionBars() {
    IActionBars bars = getViewSite().getActionBars();
//    fillLocalPullDown(bars.getMenuManager());
    fillLocalToolBar(bars.getToolBarManager());
  }

//  private void fillLocalPullDown(IMenuManager manager) {
//    manager.add(action1);
//    manager.add(new Separator());
//    manager.add(action2);
//  }

  private void fillContextMenu(IMenuManager manager) {
    ISelection selection = viewer.getSelection();
    Object obj = ((IStructuredSelection)selection).getFirstElement();
    if (obj instanceof ActionNode || obj instanceof DirectoryNode) {
      if (((TreeNode) obj).getIgnoreFailure()) {
        manager.add(action2);
      } else {
        manager.add(action1);
      }
      manager.add(new Separator());
    }
    drillDownAdapter.addNavigationActions(manager);
    // Other plug-ins can contribute there actions here
    manager.add(new Separator(IWorkbenchActionConstants.MB_ADDITIONS));
  }

  private void fillLocalToolBar(IToolBarManager manager) {
//    manager.add(action1);
//    manager.add(action2);
//    manager.add(new Separator());
    drillDownAdapter.addNavigationActions(manager);
  }

  private void makeActions() {
    action1 = new Action() {
      @Override
      public void run() {
        ISelection selection = viewer.getSelection();
        Object obj = ((IStructuredSelection)selection).getFirstElement();
        ((TreeNode) obj).setIgnoreFailure(true);
        viewer.refresh();
      }
    };
    action1.setText("Ignore failures");
    action1.setToolTipText(
        "Don't mark the parent folder as containing errors when this action fails.");
    action1.setImageDescriptor(PlatformUI.getWorkbench().getSharedImages().
      getImageDescriptor(ISharedImages.IMG_OBJS_ERROR_TSK));

    action2 = new Action() {
      @Override
      public void run() {
        ISelection selection = viewer.getSelection();
        Object obj = ((IStructuredSelection)selection).getFirstElement();
        ((TreeNode) obj).setIgnoreFailure(false);
        viewer.refresh();
      }
    };
    action2.setText("Don't ignore failures");
    action2.setToolTipText("Mark the parent folder as containing errors when this action fails.");
    action2.setImageDescriptor(PlatformUI.getWorkbench().getSharedImages().
        getImageDescriptor(ISharedImages.IMG_OBJS_ERROR_TSK));

    doubleClickAction = new Action() {
      @Override
      public void run() {
        ISelection selection = viewer.getSelection();
        Object obj = ((IStructuredSelection)selection).getFirstElement();
        try {
          ((TreeNode) obj).openEditor(getSite().getPage());
        } catch (PartInitException e) {
          MessageDialog.openError(viewer.getControl().getShell(),
                                  "Ekam Dashboard", "Couldn't open editor: " + e.getMessage());
          e.printStackTrace();
        }
      }
    };
  }

  private void hookDoubleClickAction() {
    viewer.addDoubleClickListener(new IDoubleClickListener() {
      @Override
      public void doubleClick(DoubleClickEvent event) {
        doubleClickAction.run();
      }
    });
  }

//  private void showMessage(String message) {
//    MessageDialog.openInformation(
//      viewer.getControl().getShell(),
//      "Ekam Dashboard",
//      message);
//  }

  /**
   * Passing the focus request to the viewer's control.
   */
  @Override
  public void setFocus() {
    viewer.getControl().setFocus();
  }
}