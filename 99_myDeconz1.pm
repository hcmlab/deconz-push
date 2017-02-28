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

my $deCONZ_verbose = 4;
my $deCONZ_modName = 'RaspBridge';
my $deCONZ_initialized = 0;

my %deCONZ_map_lights = ();
my %deCONZ_map_sensors = ();
my %deCONZ_map_groups = ();
my %deCONZ_map_bridges = ();

sub myDeconz1_Initialize($$)
{
	my ($hash) = @_;
	deCONZ_get_config ( 0 );
}

sub deCONZ_set_verbose {
	$deCONZ_verbose = $_[0];
}

sub deCONZ_get_uid
{
	return "deCONZ_val:";
}

sub get_uid_from_string
{
	my $isHex = 1; my $raw = $_[0];

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


sub get_uid_from_device
{
	my $key 	= $_[0];
	my $device 	= $_[1];
	my $funn 	= 'get_uid_from_device';

	my $uniqueid = $device->{uniqueid};
	if ( defined ( $uniqueid ) && length ( $uniqueid ) >= 7 ) {
		return $uniqueid;
	}			
	Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Device $key is missing a uniqueid!";

	$uniqueid = $device->{uid};
	if ( defined ( $uniqueid ) && length ( $uniqueid ) >= 7 ) {
		return $uniqueid;
	}
	Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Device $key is missing a uid!";
	
	my $reads = $device->{READINGS};
	if ( !defined ( $reads ) ) {
		Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Device $key is missing readings!";
		return undef;
	}
	
	$uniqueid = $reads->{uniqueid}{VAL};
	if ( defined ( $uniqueid ) && length ( $uniqueid ) >= 7 ) {
		return $uniqueid;
	}			
	Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Device $key is missing a uniqueid reading!";
	
	$uniqueid = $reads->{uid}{VAL};
	if ( defined ( $uniqueid ) && length ( $uniqueid ) >= 7 ) {
		return $uniqueid;
	}
	
	Log3 $deCONZ_modName, 1, "$funn: Device $key is missing a uniqueid!";
	return undef;
}


sub deCONZ_get_config
{
	my $funn = 'deCONZ_get_config';
	my %refs = ();
	my %mapping = ();
	my %groups = ();
	my $devices = $modules{HUEDevice}{defptr};
	my $tport = -1;
	my $pport = -1;
	my $verbLevel = 1;

	my $enable_push = -1;
	my $enable_fhem = -1;
	my $enable_push_socket = -1;
	my $enable_nodeupdate = -1;
	my $enable_ssl = -1;
	my $password = undef;
	my $rets = '';

	my $req_uid = 0; $req_uid = $_[0] if ( defined ( $_[0] ) );
	
	if ( !defined($devices) ) {
		Log3 $deCONZ_modName, 1, "$funn: HUEDevice not available!";
		$enable_push = 0;
		$enable_fhem = 0;
	}
	else {
		#
		# Determine number of de gateways
		#
		my $deNumber = 0;

		foreach my $key ( grep { ($defs{$_}{TYPE} eq 'HUEBridge') } keys %defs ) {
			my $bridge = $defs{$key};

			if ( !defined($bridge) ) {
				Log3 $deCONZ_modName, 1, "$funn: Bridge $key not available!";
				next;
			}

			my $manufacturer = $bridge->{manufacturer};

			if ( defined ( $manufacturer ) && index ( $manufacturer, 'dresden' ) >= 0 ) {
				my $NR = $bridge->{NR};
				my $bMac = $bridge->{mac};
				my $NAME = $bridge->{NAME};
				
				if ( !defined($NAME) || !defined($NR) || !defined($bMac) ) { next; }
				$bMac = get_uid_from_string ( $bMac );
				if ( !$bMac ) { next; }

				Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Bridge found with NR " . $NR;
				
				$deNumber++;
			}
		}

		Log3 $deCONZ_modName, 1, "$funn: Found $deNumber bridges.";

		foreach my $key ( grep { ($defs{$_}{TYPE} eq 'HUEBridge') } keys %defs ) {
			my $bridge = $defs{$key};

			if ( !defined($bridge) ) {
				Log3 $deCONZ_modName, 1, "$funn: Bridge $key not available!";
				next;
			}

			my $manufacturer = $bridge->{manufacturer};

			if ( defined ( $manufacturer ) && index ( $manufacturer, 'dresden' ) >= 0 ) {
				my $NR = $bridge->{NR};
				my $bMac = $bridge->{mac};
				my $NAME = $bridge->{NAME};
				
				if ( !defined($NAME) || !defined($NR) || !defined($bMac) ) { next; }
				$bMac = get_uid_from_string ( $bMac );
				if ( !$bMac ) { next; }

				my $ret = '';
				
				my $doRet = ( ( $deNumber == 1 || $req_uid == $bMac || $req_uid <= 0 ) ? 1 : 0 );

				$ret .= "NR:" . $NR . ";" if ( $doRet );

				Log3 $deCONZ_modName, 1, "$funn: Bridge NR " . $NR;
				
				my $deBridge;

				if ( defined ( $deCONZ_map_bridges{$NR} ) ) {
					Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Updating Bridge";
				}
				else {
					Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Creating Bridge for NR " . $NR;

					$deCONZ_map_bridges{$NR} = {};
				}
				#$deBridge = %deCONZ_map_bridges{$NR};
				$deBridge = $deCONZ_map_bridges{$NR};

				$deBridge->{hash} = \$bridge;

				if ( defined ( $deBridge->{l} ) ) {
					Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Updating lights";
				}
				else {
					Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Creating lights";
					$deBridge->{l} = {};
				}
				my $lights = $deBridge->{l};

				if ( defined ( $deBridge->{s} ) ) {
					Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Updating sensors";
				}
				else {
					Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Creating sensors";
					$deBridge->{s} = {};
				}
				my $sensors = $deBridge->{s};

				if ( defined ( $deBridge->{g} ) ) {
					Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Updating groups";
				}
				else {
					Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Creating groups";
					$deBridge->{g} = {};
				}
				my $groups = $deBridge->{g};

				my $tempval = ReadingsVal ( $key, 'push', $enable_push );
				if ( $tempval >= 0 ) {
					$enable_push = ( $tempval eq '1' ) ? 1 : 0;
					Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: enable_push $enable_push";
				}

				$tempval = ReadingsVal ( $key, 'fhemtunnel', $enable_fhem );
				if ( $tempval >= 0 ) {
					$enable_fhem = ( $tempval eq '1' ) ? 1 : 0;
					Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: enable_fhem $enable_fhem";
				}

				$tempval = ReadingsVal ( $key, 'ssl', $enable_ssl );
				if ( $tempval >= 0 ) {
					$enable_ssl = ( $tempval eq '1' ) ? 1 : 0;
					Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: ssl $enable_ssl";
				}

				$tempval = ReadingsVal ( $key, 'fpass', $password );
				if ( defined ( $tempval ) ) {
					$password = $tempval;
					Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: fpass ...";
				}

				$tempval = ReadingsVal ( $key, 'pushSocketListener', $enable_push_socket );
				if ( $tempval >= 0 ) {
					$enable_push_socket = ( $tempval eq '1' ) ? 1 : 0;
					Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: enable_push_socket $enable_push_socket";
				}

				$tempval = ReadingsVal ( $key, 'nodeUpdate', $enable_nodeupdate );
				if ( $tempval >= 0 ) {
					$enable_nodeupdate = ( $tempval eq '1' ) ? 1 : 0;
					Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: enable_nodeupdate $enable_nodeupdate";
				}

				$tempval = ReadingsVal ( $key, 'pushPort', $pport );
				
				if ( $tempval > 0 && Scalar::Util::looks_like_number ( $tempval ) ) {
					if ( $tempval > 0 && $tempval < 65000 ) {
						$pport = $tempval;
						Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: pport $pport";
					}
				}

				$tempval = ReadingsVal ( $key, 'fhemPort', $tport );

				if ( $tempval > 0 && Scalar::Util::looks_like_number ( $tempval ) ) {
					if ( $tempval > 0 && $tempval < 65000 ) {
						$tport = $tempval;
						Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: tport $tport";
					}
				}

				my $devNumber = 0;

				foreach my $key ( keys %$devices ) {
					my $device = $modules{HUEDevice}{defptr}{$key};

					if ( !defined($device) ) {
						Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Device $key not available!"; next;
					}
					
					my $name = $device->{NAME};
					
					my $IODev = $device->{IODev};
					if ( !defined ( $IODev ) ) {
						Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Device ( $name ) $key has no IODev!"; next;
					}

					if ( $IODev != $bridge ) {
						Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Device ( $name ) $key NOT from bridge."; next;
					}

					my $ID = $device->{ID};
					if ( !defined ( $ID ) ) {
						Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Device ( $name ) $key is missing an ID."; next;
					}

					my $i = index ( $ID, 'S' );
					if ( $i >= 0 ) {
						my $m = substr ( $ID, $i + 1 );
						if ( defined ( $m ) && length ( $m ) > 0 ) {
							$sensors->{$m} = $device; $devNumber++;

							Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Device $key ( $name ) -> $ID -> Sensor $m -> Added.";
						}
						else {
							Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Device $key ( $name ) -> $ID -> Sensor - NOT recognized";
						}
						next;
					}

					$i = index ( $ID, 'G' );
					if ( $i >= 0 ) {
						my $m = substr ( $ID, $i + 1 );
						if ( defined ( $m ) && length ( $m ) > 0 ) {
							$groups->{$m} = $device; $devNumber++;
							
							Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Device $key ( $name ) -> $ID -> Group $m -> Added.";
						}
						else {
							Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Device $key ( $name ) -> $ID -> Group - NOT recognized";
						}
						next;
					}

					if ( Scalar::Util::looks_like_number ( $ID ) ) {
						$lights->{$ID} = $device; $devNumber++;
						
						Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Device $key ( $name ) -> $ID -> Device -> Added.";
						next;
					}
					
					Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Device $key ( $name ) -> $ID -> NOT recognized.";
				}
				
				Log3 $deCONZ_modName, 1, "$funn: Added $devNumber devices.";

				if ( $doRet ) {
					$rets = "deCONZ_value:$ret";
					$rets .= "disable:$enable_push;" if ( $enable_push >= 0 );
					$rets .= "disablefhem:$enable_fhem;" if ( $enable_fhem >= 0 );
					$rets .= "disablepush:$enable_push_socket;" if ( $enable_push_socket >= 0 );
					$rets .= "nonodeupdate:$enable_push_socket;" if ( $enable_push_socket >= 0 );
					$rets .= "fport:$tport;" if ( $tport > 0 );
					$rets .= "pport:$pport;" if ( $pport > 0 );
					$rets .= "ssl:$pport;" if ( $enable_ssl >= 0 );
					$rets .= "fpass:$password;" if ( defined ( $password ) );
					$rets .= "\n";
				}
			}
		}
	}
	
	return $rets;
}


sub pushupd1
{
	my $funn = 'pushupd1';

	my $raw = $_[0];

	# bridge^type^id^reading^value^reading^value^...

	my @raws = split /\^/, $raw;

	my $size = $#raws + 1;

	if ( $size < 5 ) {
		Log3 $deCONZ_modName, 1, "$funn: Invalid size $size";
		return;
	}

	my $NR 		= $raws[0];
	my $deBridge = $deCONZ_map_bridges{$NR};

	if ( !defined ( $deBridge ) ) {
		Log3 $deCONZ_modName, 1, "$funn: Bridge $NR not found!";
		if ( !$deCONZ_initialized ) {
			deCONZ_get_config ( '0' );
			$deCONZ_initialized = 1;
		}
		return;
	}

	my $list;
	my $type = $raws[1];

	if ( $type eq 'l' ) {
		$list = $deBridge->{l};
	}
	elsif ( $type eq 's' ) {
		$list = $deBridge->{s};
	}
	elsif ( $type eq 'g' ) {
		$list = $deBridge->{g};
	}
	else {
		Log3 $deCONZ_modName, 1, "$funn: Invalid type $type!";
	}

	my $ID 	= $raws[2];
	my $dev 	= $list->{$ID};

	if ( !defined ( $dev ) ) {
		Log3 $deCONZ_modName, 1, "$funn: Device $type:$ID not found";
		return;
	}

	if ( $size > 5 ) {
		readingsBeginUpdate ( $dev );

		my $cur = 3;

		while ( ($cur + 1) < $size )
		{
			readingsBulkUpdate ( $dev, $raws[$cur], $raws[$cur + 1] );

			Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: $type:$ID " . $raws[$cur] . ' ' . $raws[$cur + 1];

			$cur += 2;
		}

		# If we have more than 4 updates, then we assume a complete object update.
		# No need to invoke notifies.
		readingsEndUpdate ( $dev, ( $size > 9 ? 0 : 1 ) );
	}
	else {
		readingsSingleUpdate ( $dev, $raws[3], $raws[4], 1 );

		Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: $type:$ID " . $raws[3] . ' ' . $raws[4];
	}
	return;
}


sub deCONZ_build_config
{
	my $funn = 'deCONZ_build_config';
	my %refs = ();
	my %mapping = ();
	my %groups = ();
	my $devices = $modules{HUEDevice}{defptr};
	my $tport = 7072;
	my $pport = 7073;
	my $verbLevel = 1;

	my $enable_push = 1;
	my $enable_fhem = 1;
	my $enable_push_socket = 1;
	my $enable_nodeupdate = 0;

	foreach my $key ( grep { ($defs{$_}{TYPE} eq 'HUEBridge') } keys %defs ) {
		my $bridge = $defs{$key};

		if ( !defined($bridge) ) {
			Log3 $deCONZ_modName, 1, "$funn: Bridge $key not available!";
			next;
		}

		my $manufacturer = $bridge->{manufacturer};

		if ( defined ( $manufacturer ) && index ( $manufacturer, 'dresden' ) >= 0 ) {
			Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Bridge found!";

			my $tempval = ReadingsVal ( $key, 'push', $enable_push );
			$enable_push = ( $tempval eq '1' ) ? 1 : 0;
			Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: enable_push $enable_push";

			$tempval = ReadingsVal ( $key, 'fhemtunnel', $enable_fhem );
			$enable_fhem = ( $tempval eq '1' ) ? 1 : 0;
			Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: enable_fhem $enable_fhem";

			$tempval = ReadingsVal ( $key, 'pushSocket', $enable_push_socket );
			$enable_push_socket = ( $tempval eq '1' ) ? 1 : 0;
			Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: enable_push_socket $enable_push_socket";

			$tempval = ReadingsVal ( $key, 'nodeUpdate', $enable_nodeupdate );
			$enable_nodeupdate = ( $tempval eq '1' ) ? 1 : 0;
			Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: enable_nodeupdate $enable_nodeupdate";

			$tempval = ReadingsVal ( $key, 'pushPort', $pport );
			
			if ( Scalar::Util::looks_like_number ( $tempval ) ) {
				if ( $tempval > 0 && $tempval < 65000 ) {
					$pport = $tempval;
					Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: pport $pport";
				}
			}

			$tempval = ReadingsVal ( $key, 'fhemPort', 0 );
			if ( $tempval != 0 ) {
				$tport = $tempval;
			}
			else {
				foreach my $key ( grep { ($defs{$_}{TYPE} eq 'telnet') } keys %defs ) {
					Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Telnet $key";
					my $tel = $defs{$key};

					if ( !defined($tel) || !defined($tel->{DEF}) ) {
						Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Telnet $key is not available or a connection!";
						next;
					}

					my $port = $tel->{PORT};
					if ( defined ( AttrVal ( $key, 'password', undef ) ) ) {
						Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Telnet $key requires authentication!";
						next;
					}

					Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Telnet $key -> $port!";
					$tport = $port;
					last;
				}
			}

			last;
		}
	}
	
	if ( !defined($devices) ) {
		Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: HUEDevice not available!";
		return;
	}

	foreach my $key ( keys %$devices ) {
		my $device = $modules{HUEDevice}{defptr}{$key};

		if ( !defined($device) ) {
			Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Device $key not available!";
			next;
		}

		my $nr = $device->{NR}; $refs{$nr} = $device;

		Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Device $key -> $nr!";		
	}

	foreach my $key ( sort {$a<=>$b} keys %refs ) {
		Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Device $key";

		my $device = $refs{$key};

		my $uniqueid = get_uid_from_device ( $key, $device );
		if ( !defined ( $uniqueid ) || length ( $uniqueid ) <= 0 ) {
			Log3 $deCONZ_modName, 1, "$funn: Device $key is missing a uniqueid!";
			# Check whether it is a group
			my $def = $device->{DEF};
			if ( defined ( $def ) && ( index ( $def, 'group' ) >= 0 ) ) 
			{
				my $fhemname = $device->{NAME}; my $dname = $device->{name};

				if ( defined ( $dname ) && length ( $dname ) > 0 && defined ( $fhemname ) && length ( $fhemname ) > 0 ) 
				{
					$groups{$dname} = $fhemname;
					Log3 $deCONZ_modName, 1, "$funn: Group $dname -> $fhemname";
				}
			}
			next;
		}
		
		my $fname = $device->{NAME}; my $uid = get_uid_from_string ( $uniqueid );

		if ( !Scalar::Util::looks_like_number ( $uid ) ) { next; } 

		$mapping{$uid} = $fname;

		Log3 $deCONZ_modName, 1, "$funn: Device $fname -> $uniqueid ($uid)";

		my $bridge = $device->{IODev};
		if ( defined ( $bridge ) ) 
		{
			$fname = $bridge->{NAME};
			$uniqueid = $bridge->{mac};
			
			if ( defined ( $uniqueid ) ) {
				$uid = get_uid_from_string ( $uniqueid );

				if ( Scalar::Util::looks_like_number ( $uid ) ) {
					$mapping{$uid} = $fname;					
					Log3 $deCONZ_modName, 1, "$funn: Bridge $fname -> $uniqueid ($uid)";
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
			Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Group $key : $dname";
			print $fh "1 $dname $key\n"; $gps++;
		}

		foreach my $key ( keys %mapping )
		{
			my $dname = $mapping{$key};
			Log3 $deCONZ_modName, $deCONZ_verbose, "$funn: Device uid $key : $dname";
			print $fh "$key $dname\n"; $macs++;
		}

		close $fh;
		Log3 $deCONZ_modName, 1, "$funn: Successfully added $macs device and $gps group mappings.";
	}
}


sub pushupd
{
	my $funn = 'pushupd';

	my $raw = $_[0];

	my @raws = split /\^/, $raw;

	my $size = $#raws + 1;

	if ( $size < 3 ) {
		#Log3 $deCONZ_modName, 1, "$funn: Invalid size $size";
		return;
	}

	my $device 	= $raws[0];
	my $dev 	= $defs{$device};

	if ( !defined ( $dev ) ) {
		#Log3 $deCONZ_modName, 1, "$funn: Device $device not found";
		return;
	}

	if ( $size > 3 ) {
		readingsBeginUpdate ( $dev );

		my $cur = 1;

		while ( ($cur + 1) < $size )
		{
			readingsBulkUpdate ( $dev, $raws[$cur], $raws[$cur + 1] );

			#Log3 $deCONZ_modName, 1, "$funn: " . $device . '.' . $raws[$cur] . ' ' . $raws[$cur + 1];

			$cur += 2;
		}

		# If we have more than 4 updates, then we assume a complete object update.
		# No need to invoke notifies.
		readingsEndUpdate ( $dev, ( $size > 9 ? 0 : 1 ) );
	}
	else {
		readingsSingleUpdate ( $dev, $raws[1], $raws[2], 1 );

		#Log3 $deCONZ_modName, 1, "$funn: " . $device . '.' . $raws[1] . ' ' . $raws[2];
	}
	return;
}





1;

