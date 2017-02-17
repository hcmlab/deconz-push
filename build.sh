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


MKEXT=
DBG=$1
[ "$RELB" == "" ] && RELB=1
[ -e "./.dbg" ] && RELB=0

function cmpcp
{
	if [ ! -e ./deconz-rest-plugin/$1 ]; then
		cp -f $1 ./deconz-rest-plugin/.
		[ $? != 0 ] && echo "Failed to copy file $1" && exit 1
	else
		if ! cmp $1 ./deconz-rest-plugin/$1 >/dev/null 2>&1
		then
			cp -f $1 ./deconz-rest-plugin/.
			[ $? != 0 ] && echo "Failed to replace file $1" && exit 1
		fi
	fi
}

function applypath
{
	if [ $RELB == 0 ]; then
		cmpcp $1
	else
		if [ ! -e ./deconz-rest-plugin/$1 ]; then
			[ $? != 0 ] && echo "File deconz-rest-plugin/$1 is missing." && exit 1
		fi
	
		if [ ! -e ./$1.diff ]; then
			[ $? != 0 ] && echo "File $1.diff is missing." && exit 1
		fi
	
		patch -p1 ./deconz-rest-plugin/$1 < ./$1.diff 
		[ $? != 0 ] && echo "Failed to path file $1" && exit 1
	fi
}


[[ -z "$DBG" ]] && DBG=0

if [[ $DBG == 1 ]] || [[ $DBG == debug ]]; then
	echo 'Build debug binary...'
	MKEXT=debug
	DBG=1
else
	echo 'Build release binary ...'
	DBG=0
fi

UPD=0
git --version >/dev/null 2>&1
if [ $? != 0 ]; then
	echo 'Install git?'
	echo 'It is required to download the plugin source.'
	echo ' '
    echo -n "Press return to continue or Ctrl+C to abort: "
    read response
    
	sudo apt-get update
	UPD=1
	sudo apt-get install -y git
	[ $? != 0 ] && echo 'Failed to install git ...' && exit 1
fi

qmake-qt4 --version >/dev/null 2>&1
if [ $? != 0 ]; then
	echo 'Install qt4-qmake libqt4-dev?'
	echo 'It is required to build the plugin source.'
	echo ' '
    echo -n "Press return to continue or Ctrl+C to abort: "
    read response
    
	[ $UPD == 0 ] && sudo apt-get update
	sudo apt-get install -y qt4-qmake libqt4-dev
	[ $? != 0 ] && echo 'Failed to install qt4-qmake libqt4-dev ...' && exit 1
fi

DECBIN=/usr/share/deCONZ

if [ ! -e $DECBIN ]; then
	[ -e ./deconz-latest.deb ] && rm ./deconz-latest.deb && [ $? != 0 ] && echo 'Failed to remove old deconz-latest.deb ...' && exit 1
	
	wget http://www.dresden-elektronik.de/rpi/deconz/deconz-latest.deb
	[ $? != 0 ] && echo 'Failed to download deconz latest ...' && exit 1
	
	sudo dpkg -i deconz-latest.deb
	[ $? != 0 ] && echo 'Failed to install deconz latest ...' && exit 1
	
	[ ! -e $DECBIN ] && echo 'Failed to install deconz latest ...' && exit 1
fi

DECINC=/usr/include/deconz

if [ ! -e $DECINC ]; then
	[ -e ./deconz-dev-latest.deb ] && rm ./deconz-dev-latest.deb && [ $? != 0 ] && echo 'Failed to remove old deconz-dev-latest.deb ...' && exit 1
	
	wget http://www.dresden-elektronik.de/rpi/deconz-dev/deconz-dev-latest.deb	
	[ $? != 0 ] && echo 'Failed to download deconz dev latest ...' && exit 1
	
	sudo dpkg -i deconz-dev-latest.deb
	[ $? != 0 ] && echo 'Failed to install deconz dev latest ...' && exit 1
	
	[ ! -e $DECINC ] && echo 'Failed to install deconz dev latest ...' && exit 1
fi

if [ ! -e ./deconz-rest-plugin ]; then
	git clone https://github.com/dresden-elektronik/deconz-rest-plugin.git
	[ $? != 0 ] && echo 'Failed to cloning deconz rest plugin repository ...' && exit 1
fi

[ ! -e ./deconz-rest-plugin ] && echo 'Cannot find directory ./deconz-rest-plugin ...' && exit 1

if [ $RELB == 1 ]; then
	cd ./deconz-rest-plugin
	[ $? != 0 ] && echo 'Cannot access directory ./deconz-rest-plugin ...' && exit 1
	
	# version to 2.04.18
	#git reset --hard b8393233a76fadf28a796f626c3aef428fbd4a47
	# version to 2.04.35
	git reset --hard ab00e21284faf8153aeae72b3cb9e0459ab4fff8
	
	[ $? != 0 ] && echo 'Failed to restore revision ...' && exit 1
	cd ..
fi

cmpcp ct_push_bridge.h
cmpcp ct_push_bridge.cpp

applypath rest_node_base.h
applypath rest_node_base.cpp

applypath light_node.h
applypath light_node.cpp

applypath sensor.h
applypath sensor.cpp

#applypath group.h
applypath group.cpp

applypath de_web_plugin.cpp
applypath de_web.pro

cd deconz-rest-plugin
[ $? != 0 ] && echo 'Failed to replace source files ...' && exit 1

qmake-qt4 && make $MKEXT
[ $? != 0 ] && echo 'Failed to build deconz-rest-plugin ...' && exit 1

cd ..
if [ ! -e ./libde_rest_plugin.so ]; then
	echo 'Cannot find deconz-rest-plugin ...' && exit 1
fi

if [[ $DBG == 0 ]]; then
	mv libde_rest_plugin.so libde_rest_plugin.so.rel
else
	mv libde_rest_plugin.so libde_rest_plugin.so.dbg
fi	
[ $? != 0 ] && echo 'Failed to rename deconz-rest-plugin ...' && exit 1

echo 'Installing plugin ...'
./install.sh $DBG
[ $? != 0 ] && echo 'Failed to install plugin!' && exit 1




