#!/bin/bash

# --------------------------------------------------------------------
# Copyright (C) 2016 Chi Tai Dang.
#
# @author	Chi-Tai Dang
# @version	1.0
# @remarks
#
# This extension is free software; you can redistribute it and/or modify
# it under the terms of the Eclipse Public License v1.0.
# A copy of the license may be obtained at:
# http://www.eclipse.org/org/documents/epl-v10.html
# --------------------------------------------------------------------

if [ ! -e /usr/share/deCONZ/rest_push.txt ]; then
	sudo cp -f rest_push.txt /usr/share/deCONZ/.
	[ $? != 0 ] && echo 'Failed to install rest_push.txt' && exit 1
	
	sudo chmod ogu+rwx /usr/share/deCONZ/rest_push.txt
	[ $? != 0 ] && echo 'Failed to chmod rest_push.txt' && exit 1
fi

[ -z "$FHEM_HOME" ] && FHEM_HOME=/opt/fhem

if [ -e $FHEM_HOME/FHEM ]; then
	if [ ! -e $FHEM_HOME/FHEM/99_myDeconz1.pm ]; then
		echo 'Please restart FHEM to include 99_myDeconz1.pm';
	fi
	
	sudo cp -f 99_myDeconz1.pm $FHEM_HOME/FHEM/.
	[ $? != 0 ] && echo 'Failed to update 99_myDeconz1.pm' && exit 1
else
	echo 'Warning: $FHEM_HOME/FHEM not available!'
fi

if [ ! -e ./bkp ]; then
	mkdir bkp
	[ $? != 0 ] && echo 'Failed to create backup directory ...' && exit 1
	
	cp /usr/share/deCONZ/plugins/libde_rest_plugin.so ./bkp/.
fi

if [ "$1" == "1" ]; then
	[ ! -e ./libde_rest_plugin.so.dbg ] && echo 'Debug lib libde_rest_plugin.so.dbg is missing ...' && exit 1
	
	sudo cp ./libde_rest_plugin.so.dbg /usr/share/deCONZ/plugins/libde_rest_plugin.so
	[ $? != 0 ] && echo 'Failed to install plugin ...' && exit 1

else
	[ ! -e ./libde_rest_plugin.so.rel ] && echo 'Release lib libde_rest_plugin.so.rel is missing ...' && exit 1
	
	sudo cp ./libde_rest_plugin.so.rel /usr/share/deCONZ/plugins/libde_rest_plugin.so
	[ $? != 0 ] && echo 'Failed to install plugin ...' && exit 1
fi

echo 'Please restart deCONZ process now.'
