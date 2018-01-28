#!/usr/bin/python

# Copyright (C) 2001-2012 Artifex Software, Inc.
# All Rights Reserved.
#
# This software is provided AS-IS with no warranty, either express or
# implied.
#
# This software is distributed under license and may not be copied,
# modified or distributed except as expressly authorized under the terms
# of the license contained in the file LICENSE in this distribution.
#
# Refer to licensing information at http://www.artifex.com or contact
# Artifex Software, Inc.,  7 Mt. Lassen Drive - Suite A-134, San Rafael,
# CA  94903, U.S.A., +1(415)492-9861, for further information.
#

# noddy script to rejig the git log output into a Ghostscript html changelog

import os
import sys
import string
import datetime
import time
import codecs

argc = len(sys.argv)
if argc < 3:
  sys.stderr.write("Usage: gitlog2changelog.py <starting ref> <ending ref>\n")
  sys.stderr.write("For example: gitlog2changelog.py b246d9d 0805588\n")

else:
  sys.stdout.write ("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\"   \"http://www.w3.org/TR/html4/strict.dtd\">\n")
  sys.stdout.write ("<html>\n<head>\n<title>\nGhostscript change history\n</title>\n")
  sys.stdout.write ("<!-- generated by gitlog2changelog.py -->\n")
  sys.stdout.write ("<link rel=stylesheet type=\"text/css\" href=\"gs.css\">\n")
  sys.stdout.write ("</head>\n")

  sys.stdout.write ("<body>\n")

  # Create a list of commit SHA1 sums we want to log
  commit_list = []
  cmd="git log --topo-order --pretty=oneline " + sys.argv[1] + "..." + sys.argv[2]
  res = os.popen(cmd, "r")
  line1=res.readline()

  while line1:
    line2 = line1.rstrip()
    commit_list.append(str.split(line2)[0])
    line1=res.readline()

  res.close()

  # Now traverse that list getting the commit messages for each
  for csum in commit_list:
    # we have to use the slightly baroque syntax: git log --cc --topo-order <commit>^...<commit>
    # where the "^" indicates the commit prior to the one we're processing with
    cmd="git log --name-only --topo-order --date=iso -n1 " + csum + "^" + "..." + csum
    res = os.popen(cmd, "r")
    commit=res.readlines()
    # This assumes the order of the lines.....
    sys.stdout.write("<p><strong>")
    if str.find(commit[1], "Merge:") < 0:
      nm = commit[1]
      dt = commit[2]
      logidx = 3
    else:
      nm = commit[2]
      dt = commit[3]
      logidx = 4

    sys.stdout.write (dt.split("Date:")[1].strip())
    sys.stdout.write ("\n")
    auth_name=nm.split("Author: ")[1].strip()
    sys.stdout.write ("</strong>\n<br>" + auth_name.replace("<", "&lt;").replace(">", "&gt;") + "<br>\n")
    sys.stdout.write ("<a href=\"http://git.ghostscript.com/?p=ghostpdl.git;a=commitdiff;h=" + commit[0].split("commit ")[1].strip() + "\">")
    sys.stdout.write (commit[0].split("commit ")[1].strip() + "</a>\n")

    sys.stdout.write ("<blockquote>\n")
    sys.stdout.write ("<p>\n")
    log = commit[logidx:]

    marked = 0
    # this loop needs to skip initial blank lines
    for logline in log:
      if len(logline.strip()) == 0 and marked == 0 :
        continue

      sys.stdout.write (logline.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace("\"", "&quot;").rstrip() + "<br>\n")
      marked = 1

    sys.stdout.write ("<p>\n")
    sys.stdout.write ("</blockquote>\n")
    sys.stdout.write ("<hr>\n")

  sys.stdout.write ("<hr size=20>\n\n\n")
  sys.stdout.write ("</body>\n")
  sys.stdout.write ("</html>\n")
