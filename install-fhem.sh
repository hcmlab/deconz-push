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


[ -z "$FHEM_HOME" ] && FHEM_HOME=/opt/fhem

if [ -e $FHEM_HOME/FHEM ]; then
	if [ ! -e $FHEM_HOME/FHEM/99_myDeconz1.pm ]; then
		echo 'Please restart FHEM to include 99_myDeconz1.pm';
	fi
	
	sudo cp -f 99_myDeconz1.pm $FHEM_HOME/FHEM/.
	[ $? != 0 ] && echo 'Failed to update 99_myDeconz1.pm' && exit 1
			
	echo "Installed 99_myDeconz1.pm"
else
	echo 'Warning: $FHEM_HOME/FHEM not available!'
fi

