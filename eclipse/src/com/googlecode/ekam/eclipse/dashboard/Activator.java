package com.googlecode.ekam.eclipse.dashboard;

import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.DecorationOverlayIcon;
import org.eclipse.jface.viewers.IDecoration;
import org.eclipse.swt.graphics.Image;
import org.eclipse.ui.ISharedImages;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.plugin.AbstractUIPlugin;
import org.osgi.framework.BundleContext;

/**
 * The activator class controls the plug-in life cycle
 */
public class Activator extends AbstractUIPlugin {

  // The plug-in ID
  public static final String PLUGIN_ID = "com.googlecode.ekam.eclipse.dashboard"; //$NON-NLS-1$

  // The shared instance
  private static Activator plugin;

  // Images
  static Image DIRECTORY_IMG;
  static Image DIRECTORY_RUNNING_IMG;
  static Image DIRECTORY_WITH_ERRORS_IMG;
  static Image DIRECTORY_WITH_ERRORS_IGNORED_IMG;
  static Image DELETED_IMG;
  static Image PENDING_IMG;
  static Image RUNNING_IMG;
  static Image DONE_IMG;
  static Image PASSED_IMG;
  static Image FAILED_IMG;
  static Image FAILED_IGNORED_IMG;
  static Image BLOCKED_IMG;

  /**
   * The constructor
   */
  public Activator() {
  }

  /*
   * (non-Javadoc)
   * @see org.eclipse.ui.plugin.AbstractUIPlugin#start(org.osgi.framework.BundleContext)
   */
  @Override
  public void start(BundleContext context) throws Exception {
    super.start(context);
    plugin = this;

    DIRECTORY_IMG = getSharedImage(ISharedImages.IMG_OBJ_FOLDER);
    DIRECTORY_RUNNING_IMG = overlay(DIRECTORY_IMG, "icons/running_overlay.gif");
    DIRECTORY_WITH_ERRORS_IMG = overlay(DIRECTORY_IMG, "icons/error_overlay.gif");
    DIRECTORY_WITH_ERRORS_IGNORED_IMG = overlay(DIRECTORY_IMG, "icons/error_ignored_overlay.gif");

    DELETED_IMG = getImage("icons/deleted.gif");
    PENDING_IMG = getImage("icons/pending.gif");
    RUNNING_IMG = overlay(PENDING_IMG, "icons/running_overlay.gif");
    DONE_IMG = getImage("icons/done.gif");
    PASSED_IMG = getImage("icons/passed.gif");
    FAILED_IMG = getImage("icons/failed.gif");
    FAILED_IGNORED_IMG = overlay(PENDING_IMG, "icons/error_ignored_overlay.gif");
    BLOCKED_IMG = getImage("icons/blocked.gif");
  }

  private Image getImage(String path) {
    return getImageDescriptor(path).createImage();
  }

  private Image overlay(Image base, String overlayPath) {
    return new DecorationOverlayIcon(
        base, getImageDescriptor(overlayPath), IDecoration.BOTTOM_RIGHT).createImage();
  }

  private Image getSharedImage(String imageKey) {
    return PlatformUI.getWorkbench().getSharedImages().getImage(imageKey);
  }

  /*
   * (non-Javadoc)
   * @see org.eclipse.ui.plugin.AbstractUIPlugin#stop(org.osgi.framework.BundleContext)
   */
  @Override
  public void stop(BundleContext context) throws Exception {
    // DIRECTORY_IMG is a shared image; don't dispose.
    DIRECTORY_RUNNING_IMG.dispose();
    DIRECTORY_WITH_ERRORS_IMG.dispose();
    DIRECTORY_WITH_ERRORS_IGNORED_IMG.dispose();
    DELETED_IMG.dispose();
    PENDING_IMG.dispose();
    RUNNING_IMG.dispose();
    DONE_IMG.dispose();
    PASSED_IMG.dispose();
    FAILED_IMG.dispose();
    FAILED_IGNORED_IMG.dispose();
    BLOCKED_IMG.dispose();

    plugin = null;
    super.stop(context);
  }

  /**
   * Returns the shared instance
   *
   * @return the shared instance
   */
  public static Activator getDefault() {
    return plugin;
  }

  /**
   * Returns an image descriptor for the image file at the given
   * plug-in relative path
   *
   * @param path the path
   * @return the image descriptor
   */
  public static ImageDescriptor getImageDescriptor(String path) {
    return imageDescriptorFromPlugin(PLUGIN_ID, path);
  }
}
