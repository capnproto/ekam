package com.googlecode.ekam.eclipse.dashboard;

import java.net.URL;

import org.eclipse.core.runtime.FileLocator;
import org.eclipse.core.runtime.Path;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.swt.graphics.Image;
import org.eclipse.ui.plugin.AbstractUIPlugin;
import org.osgi.framework.Bundle;
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
  static Image DELETED_IMG;
  static Image PENDING_IMG;
  static Image RUNNING_IMG;
  static Image DONE_IMG;
  static Image PASSED_IMG;
  static Image FAILED_IMG;
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

    Bundle bundle = context.getBundle();

    DELETED_IMG = getImage(bundle, "icons/deleted.gif");
    PENDING_IMG = getImage(bundle, "icons/pending.gif");
    RUNNING_IMG = getImage(bundle, "icons/running.gif");
    DONE_IMG    = getImage(bundle, "icons/done.gif");
    PASSED_IMG  = getImage(bundle, "icons/passed.gif");
    FAILED_IMG  = getImage(bundle, "icons/failed.gif");
    BLOCKED_IMG = getImage(bundle, "icons/blocked.gif");
  }

  private Image getImage(Bundle bundle, String path) {
    URL url = FileLocator.find(bundle, new Path(path), null);
    return ImageDescriptor.createFromURL(url).createImage();
  }

  /*
   * (non-Javadoc)
   * @see org.eclipse.ui.plugin.AbstractUIPlugin#stop(org.osgi.framework.BundleContext)
   */
  @Override
  public void stop(BundleContext context) throws Exception {
    DELETED_IMG.dispose();
    PENDING_IMG.dispose();
    RUNNING_IMG.dispose();
    DONE_IMG.dispose();
    PASSED_IMG.dispose();
    FAILED_IMG.dispose();
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
