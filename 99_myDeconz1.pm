##############################################
# $Id: 99_myDeconz1.pm 7570 2016-10-16 18:31:44Z chi-tai $
package main;

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

use strict;
use warnings;
use POSIX;

use CGI qw(:standard);
use Data::Dumper;

sub
myDeconz1_Initialize($$)
{
  my ($hash) = @_;
}


sub
get_uid_from_string
{
	my $isHex = 1;
	my $raw = $_[0];

	my $i = index ( $raw, '-' );
	if ( $i > 0 ) { $raw = substr ( $raw, 0, $i ); }

	if ( index ( $raw, ':' ) > 0 ) { $raw =~ s/://g; }
	elsif ( index ( $raw, 'x') < 0 ) { $isHex = 0; }

	if ( $isHex ) {
		no warnings 'portable';
		$raw = hex ( $raw );
	}
	return $raw;
}


sub
deCONZ_build_config
{
	my $name = 'RaspBridge';
	my $funn = 'deCONZ_build_config';
	my %refs = ();
	my %mapping = ();
	my %groups = ();
	my $devices = $modules{HUEDevice}{defptr};
	my $tport = 7072;
	my $pport = 7073;

	my $enable_push = 1;
	my $enable_fhem = 1;
	my $enable_push_socket = 1;
	my $enable_nodeupdate = 0;

	foreach my $key ( grep { ($defs{$_}{TYPE} eq 'HUEBridge') } keys %defs ) {
		my $bridge = $defs{$key};

		if ( !defined($bridge) ) {
			Log3 $name, 1, "$funn: Bridge $key not available!";
			next;
		}

		my $manufacturer = $bridge->{manufacturer};

		if ( index ( $manufacturer, 'dresden' ) >= 0 ) {
			Log3 $name, 4, "$funn: Bridge found!";

			my $tempval = ReadingsVal ( $key, 'push', $enable_push );
			$enable_push = ( $tempval eq '1' ) ? 1 : 0;
			Log3 $name, 4, "$funn: enable_push $enable_push";

			$tempval = ReadingsVal ( $key, 'fhemtunnel', $enable_fhem );
			$enable_fhem = ( $tempval eq '1' ) ? 1 : 0;
			Log3 $name, 4, "$funn: enable_fhem $enable_fhem";

			$tempval = ReadingsVal ( $key, 'pushSocket', $enable_push_socket );
			$enable_push_socket = ( $tempval eq '1' ) ? 1 : 0;
			Log3 $name, 4, "$funn: enable_push_socket $enable_push_socket";

			$tempval = ReadingsVal ( $key, 'nodeUpdate', $enable_nodeupdate );
			$enable_nodeupdate = ( $tempval eq '1' ) ? 1 : 0;
			Log3 $name, 4, "$funn: enable_nodeupdate $enable_nodeupdate";

			$tempval = ReadingsVal ( $key, 'pushPort', $pport );
			
			if ( Scalar::Util::looks_like_number ( $tempval ) ) {
				if ( $tempval > 0 && $tempval < 65000 ) {
					$pport = $tempval;
					Log3 $name, 4, "$funn: pport $pport";
				}
			}

			$tempval = ReadingsVal ( $key, 'fhemPort', 0 );
			if ( $tempval != 0 ) {
				$tport = $tempval;
			}
			else {
				foreach my $key ( grep { ($defs{$_}{TYPE} eq 'telnet') } keys %defs ) {
					Log3 $name, 4, "$funn: Telnet $key";
					my $tel = $defs{$key};

					if ( !defined($tel) || !defined($tel->{DEF}) ) {
						Log3 $name, 4, "$funn: Telnet $key is not available or a connection!";
						next;
					}

					my $port = $tel->{PORT};
					if ( defined ( AttrVal ( $key, 'password', undef ) ) ) {
						Log3 $name, 4, "$funn: Telnet $key requires authentication!";
						next;
					}

					Log3 $name, 4, "$funn: Telnet $key -> $port!";
					$tport = $port;
					last;
				}
			}

			last;
		}
	}
	
	if ( !defined($devices) ) {
		Log3 $name, 4, "$funn: HUEDevice not available!";
		return;
	}

	foreach my $key ( keys %$devices ) {
		my $device = $modules{HUEDevice}{defptr}{$key};

		if ( !defined($device) ) {
			Log3 $name, 4, "$funn: Device $key not available!";
			next;
		}

		my $nr = $device->{NR}; $refs{$nr} = $device;

		Log3 $name, 4, "$funn: Device $key -> $nr!";		
	}

	foreach my $key ( sort {$a<=>$b} keys %refs ) {
		Log3 $name, 4, "$funn: Device $key";

		my $device = $refs{$key};

		my $uniqueid = $device->{uniqueid};
		if ( !defined ( $uniqueid ) || length ( $uniqueid ) <= 0 ) {
			Log3 $name, 4, "$funn: Device $key is missing a uniqueid!";
			# Check whether it is a group
			my $def = $device->{DEF};
			if ( defined ( $def ) && ( index ( $def, 'group' ) >= 0 ) ) 
			{
				my $fhemname = $device->{NAME}; my $dname = $device->{name};

				if ( defined ( $dname ) && length ( $dname ) > 0 && defined ( $fhemname ) && length ( $fhemname ) > 0 ) 
				{
					$groups{$dname} = $fhemname;
					Log3 $name, 4, "$funn: Group $dname -> $fhemname";
				}
			}
			next;
		}
		
		my $fname = $device->{NAME}; my $uid = get_uid_from_string ( $uniqueid );

		if ( !Scalar::Util::looks_like_number ( $uid ) ) { next; } 

		$mapping{$uid} = $fname;

		Log3 $name, 4, "$funn: Device $fname -> $uniqueid ($uid)";

		my $bridge = $device->{IODev};
		if ( defined ( $bridge ) ) 
		{
			$fname = $bridge->{NAME};
			$uniqueid = $bridge->{mac};
			
			if ( defined ( $uniqueid ) ) {
				$uid = get_uid_from_string ( $uniqueid );

				if ( Scalar::Util::looks_like_number ( $uid ) ) {
					$mapping{$uid} = $fname;					
					Log3 $name, 4, "$funn: Bridge $fname -> $uniqueid ($uid)";
				} 
			}
		}
	}

	my $macs = 0; my $gps = 0;

	my $mapFile = '/usr/share/deCONZ/rest_push.txt';
	open ( my $fh, '>', $mapFile );

	if ( $fh ) {
		print $fh "2 fport $tport\n";
		print $fh "2 pport $pport\n";
		
		my $prep = '';
		
		$prep = ( $enable_push == 1 ) ? '#' : '';
		print $fh $prep . "0 disable\n"; # Disable plugin
		
		$prep = ( $enable_fhem == 1 ) ? '#' : '';
		print $fh $prep . "0 disablefhem\n"; # Disable fhem tunnel
		
		$prep = ( $enable_push_socket == 1 ) ? '#' : '';
		print $fh $prep . "0 disablepush\n"; # Disable push listener socket
		
		$prep = ( $enable_nodeupdate == 1 ) ? '#' : '';
		print $fh $prep ."0 nonodeupdate\n"; # Skip updated of deCONZ rest-node on detection of changes in event handler
		
		foreach my $key ( keys %groups )
		{
			my $dname = $groups{$key};
			Log3 $name, 4, "$funn: Group $key : $dname";
			print $fh "1 $dname $key\n"; $gps++;
		}

		foreach my $key ( keys %mapping )
		{
			my $dname = $mapping{$key};
			Log3 $name, 4, "$funn: Device uid $key : $dname";
			print $fh "$key $dname\n"; $macs++;
		}

		close $fh;
		Log3 $name, 1, "$funn: Successfully added $macs device and $gps group mappings.";
	}
}








1;

