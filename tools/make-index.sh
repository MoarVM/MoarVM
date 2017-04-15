#!/bin/bash
# Adapted from script by 'Peter van der Does' on website stackoverflow.com
# CC-by-SA
# https://stackoverflow.com/questions/21395159/shell-script-to-create-a-static-html-directory-listing
root="."
echo "<ul>"
DEPTH=1
files="$(find $root -maxdepth $DEPTH)"
for file in $files; do
  parentpath="${file#*/}"
  parent="${parentpath%/*}"
  filename="${file##*/}"
  if [[ -z $oldparent ]]; then
    echo "  <li> $parent </li>" && oldparent="$parent"
    echo "  <ul>"
  elif [[ $oldparent != $parent ]]; then
    echo "  </ul>"
    echo "  <li> $parent </li>" && oldparent="$parent"
    echo "  <ul>"
  fi
  echo "    <li><a href=\"$parentpath\">$filename</a></li>"
done
echo "  </ul>"
echo "</ul>"
