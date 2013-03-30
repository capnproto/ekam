package com.googlecode.ekam.eclipse.dashboard;

import java.io.IOException;
import java.io.InputStream;
import java.io.InterruptedIOException;
import java.net.Socket;
import java.util.HashMap;
import java.util.LinkedList;
import java.util.Map;
import java.util.Queue;

import org.eclipse.swt.widgets.Display;

import com.googlecode.ekam.proto.DashboardProto.Header;
import com.googlecode.ekam.proto.DashboardProto.TaskUpdate;

public class EventThread implements Runnable {
  private final DirectoryNode root;
  private final Runnable onChange;

  private InputStream input;
  private final Map<Integer, ActionNode> actionsById = new HashMap<Integer, ActionNode>();
  private final Queue<TaskUpdate> updateQueue = new LinkedList<TaskUpdate>();

  public EventThread(DirectoryNode root, Runnable onChange) {
    this.root = root;
    this.onChange = onChange;
  }

  private void reconnect() throws IOException {
    Socket socket = new Socket("localhost", 51315);
    input = socket.getInputStream();
  }

  private final Runnable applyUpdates = new Runnable() {
    @Override
    public void run() {
      synchronized (updateQueue) {
        while (true) {
          TaskUpdate update = updateQueue.poll();
          if (update == null) {
            break;
          }

          ActionNode action = actionsById.get(update.getId());
          if (action == null) {
            if (!update.hasNoun()) {
              // TODO:  Report error via UI.
              System.err.println("Update for new task ID had no noun.");
              continue;
            }
            action = root.newAction(update.getNoun(), update);
            actionsById.put(update.getId(), action);
          } else {
            if (!action.applyUpdate(update)) {
              actionsById.remove(update.getId());
            }
          }
        }
      }

      onChange.run();
    }
  };

  private final Runnable applyUpdatesLater = new Runnable() {
    @Override
    public void run() {
      Display.getDefault().timerExec(100, applyUpdates);
    }
  };

  private final Runnable clearAll = new Runnable() {
    @Override
    public void run() {
      root.clear();
      actionsById.clear();
      updateQueue.clear();
    }
  };

  @Override
  public void run() {
    while (!Thread.interrupted()) {
      try {
        reconnect();

        // Skip the header.
        Header.parseDelimitedFrom(input);

        while (true) {
          TaskUpdate update = TaskUpdate.parseDelimitedFrom(input);
          if (update == null) {
            break;
          }

          if (Thread.interrupted()) {
            // For some reason, InterruptedIOException doesn't seem to be
            // thrown when interrupted during read().
            return;
          }

          boolean needEvent;
          synchronized (updateQueue) {
            needEvent = updateQueue.isEmpty();
            updateQueue.offer(update);
          }

          if (needEvent) {
            // TODO:  Is there an Executor somewhere that I could use instead?
            Display.getDefault().asyncExec(applyUpdatesLater);
          }
        }
      } catch (InterruptedIOException e) {
        return;
      } catch (IOException e) {
        Display.getDefault().asyncExec(clearAll);
        try {
          Thread.sleep(10000);
        } catch (InterruptedException e1) {
          return;
        }
      } finally {
        try {
          if (input != null) {
            input.close();
          }
        } catch (IOException e) {
          // TODO:  Report error via UI.
          e.printStackTrace();
        }
      }
    }
  }
}
