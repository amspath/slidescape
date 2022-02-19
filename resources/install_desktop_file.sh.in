#!/usr/bin/bash

SCRIPT_DIR=$(dirname "${BASH_SOURCE[0]}")

if test -f "$SCRIPT_DIR/slidescape.desktop"; then

  if test -f "$SCRIPT_DIR/slidescape"; then
    xdg-desktop-menu install --novendor "$SCRIPT_DIR/slidescape.desktop"
    xdg-icon-resource install --context apps --size 16 --novendor "$SCRIPT_DIR/resources/icon/icon16.png" slidescape
    xdg-icon-resource install --context apps --size 32 --novendor "$SCRIPT_DIR/resources/icon/icon32.png" slidescape
    xdg-icon-resource install --context apps --size 64 --novendor "$SCRIPT_DIR/resources/icon/icon64.png" slidescape
    xdg-icon-resource install --context apps --size 128 --novendor "$SCRIPT_DIR/resources/icon/icon128.png" slidescape
  else
    echo "Compiled executable 'slidescape' is missing - nothing to install."
  fi
else
  echo "slidescape.desktop is not configured/missing - nothing to install."
fi
