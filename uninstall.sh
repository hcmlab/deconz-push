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

if [ -e /opt/fhem/FHEM/99_myDeconz1.pm ]; then
	sudo rm /opt/fhem/FHEM/99_myDeconz1.pm
	[ $? != 0 ] && echo 'Failed to remove 99_myDeconz1.pm'
fi

if [ -e ./bkp/libde_rest_plugin.so ]; then
	sudo cp ./bkp/libde_rest_plugin.so /usr/share/deCONZ/plugins/.
	[ $? != 0 ] && echo 'Failed to restore original rest-plugin ...' && exit 1
else
	[ $? != 0 ] && echo 'Backup of original rest-plugin not available!' && exit 1
fi

echo 'Please restart deCONZ process now.'
