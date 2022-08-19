#!/bin/sh -e

git init --bare $HOME/cdba-admin
install -m 755 post-receive $HOME/cdba-admin/hooks/
install -m 755 update $HOME/cdba-admin/hooks/

mkdir -p $HOME/bin
install -m 755 cdba-shell $HOME/bin/
