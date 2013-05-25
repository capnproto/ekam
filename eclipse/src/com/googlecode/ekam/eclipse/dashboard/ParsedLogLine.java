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

  private static final Pattern EKAM_GARBAGE_PATTERN =
      Pattern.compile("/ekam-provider/[^/: ]*/");
  private static final Pattern FILENAME_PATTERN =
    Pattern.compile("([^: ]+):");
  private static final Pattern LOCATION_PATTERN =
    Pattern.compile("([0-9]+):");
//  private static final Pattern RANGE_PATTERN =
//      Pattern.compile("[{]([0-9]+):([0-9]+)-([0-9]+):([0-9]+)[}]");
  private static final Pattern TYPE_PATTERN =
    Pattern.compile(" (fatal error|error|warning|note):");

  public ParsedLogLine(String text) {
    // Trim off confusing /ekam-provider/ garbage.
    {
      Matcher matcher = EKAM_GARBAGE_PATTERN.matcher(text);
      if (matcher.lookingAt()) {
        text = text.substring(matcher.end());
      }
    }

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

//    // Interpret Clang ranges if available.  For the moment we don't try because Eclipse makes
//    // it too difficult to use this information anyway.
//    while (true) {
//      Matcher matcher = RANGE_PATTERN.matcher(text);
//      if (!matcher.lookingAt()) {
//        break;
//      }
//
//      int rangeLineStart = Integer.parseInt(matcher.group(1));
//      int rangeColStart = Integer.parseInt(matcher.group(2));
//      int rangeLineEnd = Integer.parseInt(matcher.group(3));
//      int rangeColEnd = Integer.parseInt(matcher.group(4));
//      text = text.substring(matcher.end());
//
//      if (text.startsWith(":")) {
//        text = text.substring(1);
//        break;
//      }
//    }

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
