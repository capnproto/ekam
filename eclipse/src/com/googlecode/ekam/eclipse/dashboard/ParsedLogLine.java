package com.googlecode.ekam.eclipse.dashboard;

import java.util.regex.Matcher;
import java.util.regex.Pattern;

class ParsedLogLine {
  public enum Type {
    ERROR,
    WARNING,
    NOTE,
    PREFIX
  }

  public final String fullText;

  public final Type type;
  public final String filename;
  public final int locationLine;
  public final int locationColumn;
  public final String message;

  private static final Pattern FILENAME_PATTERN =
    Pattern.compile("([^: ]+):");
  private static final Pattern LOCATION_PATTERN =
    Pattern.compile("([0-9]+):");
  private static final Pattern TYPE_PATTERN =
    Pattern.compile(" (fatal error|error|warning|note):");

  public ParsedLogLine(String text) {
    fullText = text;

    {
      Matcher matcher = FILENAME_PATTERN.matcher(text);
      if (matcher.lookingAt()) {
        filename = matcher.group(1);
        text = text.substring(matcher.end());
      } else {
        filename = null;
      }
    }

    {
      Matcher matcher = LOCATION_PATTERN.matcher(text);
      if (matcher.lookingAt()) {
        locationLine = Integer.parseInt(matcher.group(1));
        text = text.substring(matcher.end());
      } else {
        locationLine = -1;
      }
    }

    {
      Matcher matcher = LOCATION_PATTERN.matcher(text);
      if (matcher.lookingAt()) {
        locationColumn = Integer.parseInt(matcher.group(1));
        text = text.substring(matcher.end());
      } else {
        locationColumn = -1;
      }
    }

    {
      Matcher matcher = TYPE_PATTERN.matcher(text);
      if (matcher.lookingAt()) {
        String typeName = matcher.group(1);
        text = text.substring(matcher.end());

        if (typeName.equals("error") || typeName.equals("fatal error")) {
          type = Type.ERROR;
        } else if (typeName.equals("warning")) {
          type = Type.WARNING;
        } else if (typeName.equals("note")) {
          type = Type.NOTE;
        } else {
          throw new IllegalStateException("can't get here");
        }
      } else {
        type = Type.PREFIX;
      }
    }

    message = text.trim();
  }
}
