#!/bin/bash

# --------------------------------------------------------------------
# Copyright (C) 2016 Chi Tai Dang.
#
# @author	Chi-Tai Dang
# @version	1.1
# @remarks
#
# This extension is free software; you can redistribute it and/or modify
# it under the terms of the Eclipse Public License v1.0.
# A copy of the license may be obtained at:
# http://www.eclipse.org/org/documents/epl-v10.html
# --------------------------------------------------------------------

function cmpcp
{
	if [ ! -e /usr/share/deCONZ/$1 ]; then
		sudo cp -f $1 /usr/share/deCONZ/.
		[ $? != 0 ] && echo "Failed to install $1" && exit 1
	
		if [ $2 == 1 ]; then
			sudo chmod ogu+rwx /usr/share/deCONZ/$1
		else
			sudo chmod ug+rwx /usr/share/deCONZ/$1
			echo "Installed $1. You should update the configuration through this file."
		fi
		[ $? != 0 ] && echo "Failed to chmod $1" && exit 1
	fi
}

cmpcp rest_push.conf 0
cmpcp rest_bridge.conf 1

./install-fhem.sh

if [ ! -e ./bkp ]; then
	mkdir bkp
	[ $? != 0 ] && echo 'Failed to create backup directory ...' && exit 1
	
	cp /usr/share/deCONZ/plugins/libde_rest_plugin.so ./bkp/.
fi
exit 0

if [ "$1" == "1" ]; then
	[ ! -e ./libde_rest_plugin.so.dbg ] && echo 'Debug lib libde_rest_plugin.so.dbg is missing ...' && exit 1
	
	sudo cp ./libde_rest_plugin.so.dbg /usr/share/deCONZ/plugins/libde_rest_plugin.so
	[ $? != 0 ] && echo 'Failed to install plugin ...' && exit 1

else
	[ ! -e ./libde_rest_plugin.so.rel ] && echo 'Release lib libde_rest_plugin.so.rel is missing ...' && exit 1
	
	sudo cp ./libde_rest_plugin.so.rel /usr/share/deCONZ/plugins/libde_rest_plugin.so
	[ $? != 0 ] && echo 'Failed to install plugin ...' && exit 1
fi

DECSTART=
if [ -e /etc/init.d/deCONZ ]; then
	DECSTART=/etc/init.d/deCONZ
else
	if [ -e /etc/init.d/deconz ]; then
		DECSTART=/etc/init.d/deconz
	else
		if [ -e /etc/init.d/deConz ]; then
			DECSTART=/etc/init.d/deConz
		else
			echo 'Please restart deCONZ process now.'
		fi
	fi
fi

if [ "$DECSTART" != "" ]; then
	sudo $DECSTART stop
	sudo $DECSTART start
	
	[ $? != 0 ] && echo 'Please restart deCONZ process now.'
fi

[ -e "/opt/fhem" ] && echo "Please enter 'reload 99_myDeconz1.pm' in the fhem command box."
exit 0
