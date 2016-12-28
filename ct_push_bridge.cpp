/**
* --------------------------------------------------------------------
* Copyright (C) 2016 Chi Tai Dang.
*
* @author	Chi-Tai Dang
* @version	1.0
* @remarks
*
* This extension is free software; you can redistribute it and/or modify
* it under the terms of the Eclipse Public License v1.0.
* A copy of the license may be obtained at:
* http://www.eclipse.org/org/documents/epl-v10.html
* --------------------------------------------------------------------
*/

#include <QtPlugin>
#include <QTimer>
#include "ct_push_bridge.h"
#include "rest_node_base.h"
#include "light_node.h"
#include "sensor.h"
#include <deconz.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <fstream>
#include <sstream>
#include <iostream>       // std::cerr
#include <exception>
#include <time.h>
#include <sys/stat.h>

#ifdef DEBUG
#include <execinfo.h>
#include <signal.h>
#endif

using namespace std;


#ifdef ENABLE_PUSH

//#define DEBUGV3

/*
#ifdef DEBUGV2
#   undef DEBUGV2
#   define DEBUGV2(a)
#endif

#ifdef DEBUGV20
#   undef DEBUGV20
#   define DEBUGV20(a)
#endif

#ifdef DEBUGV1
#   undef DEBUGV1
#   define DEBUGV1(a)
#endif

#ifdef DEBUGV10
#   undef DEBUGV10
#   define DEBUGV10(a)
#endif
*/

////////////////////////////////////////////////////////////////////////////////
// PushBridge statics
////////////////////////////////////////////////////////////////////////////////

PushBridge pushBridge;
PushDevice raspBee;

const char * UNKNOWN_DEVICE_NAME = "Unknown";

bool    PushBridge::enable_plugin       = true;
bool    PushBridge::enable_push         = true;
bool    PushBridge::enable_fhem_tunnel  = true;
bool    PushBridge::fhem_node_update    = true;
int     PushBridge::fhem_port           = 7072;
int     PushBridge::push_port           = 7073;

const char *    configFile      = "/usr/share/deCONZ/rest_push.txt";
time_t          configFileTS    = 0;

#ifdef DEBUG
void sigseghandler ( int sig )
{
  void * array [ 30 ];
  size_t size;

  size = backtrace ( array, 30 );

  fprintf ( stderr, "Stacktrace: Signal %d\n", sig );
  backtrace_symbols_fd ( array, size, STDERR_FILENO );
  exit ( 1 );
}
#endif

void PushBridge::RequestConfigFile ()
{
    DEBUGFTRACE ( "RequestConfigFile" );

    uint64_t t = time ( 0 );
    uint64_t diff = t - lastConfigRequest;
    if ( diff < 60 )
        return;
    
    if ( diff < 180 ) {
        configRequested++;
    }

    if ( configRequested >= 20 ) {
        if ( diff > 3600 )
            configRequested = 0;
        else return;
    }

    lastConfigRequest = t;
    
    EnqueueToFhem ( "{deCONZ_build_config()}\n" );
}


void PushBridge::CheckConfigFile ()
{
    DEBUGFTRACE ( "CheckConfigFile" );

    struct stat attr;
    if ( stat ( configFile, &attr ) ) {
        return; // File does not exist
    }
    else {
        if ( configFileTS == attr.st_mtime ) {
            return; // File has not changed
        }

        configFileTS = attr.st_mtime;
    }

    ifstream mapFile ( configFile );

    if ( mapFile.good () )
    {
        DEBUGV ( "CheckConfigFile: Loading ..." );

        if ( !pthread_mutex_lock ( &devicesLock ) )
        {
            if ( !pthread_mutex_lock ( &groupLock ) )
            {
                string line;
                while ( getline ( mapFile, line ) )
                {
                    std::istringstream iss ( line );
                    
                    const char * chars = line.c_str ();
                    if ( line.size () > 0 && chars [ 0 ] == '#' )
                        continue;

                    if ( chars [ 0 ] == '0' && chars [ 1 ] == ' ' ) {
                        // We have a configuration option
                        int idname; string cfg;

                        if ( iss >> idname >> cfg ) {
                            // Disable push extension
                            if ( !cfg.compare ( "disable" ) ) {
                                enable_plugin = false;
                                DEBUGV1 ( "CheckConfigFile: Disabled push extension." );
                                break;
                            }
                            else if ( !cfg.compare ( "nonodeupdate" ) ) {
                                fhem_node_update = false;
                                DEBUGV1 ( "CheckConfigFile: Disabled fhem node update." );
                            }
                            else if ( !cfg.compare ( "disablefhem" ) ) {
                                enable_fhem_tunnel = false;
                                DEBUGV1 ( "CheckConfigFile: Disabled fhem tunnel." );
                            }
                            else if ( !cfg.compare ( "disablepush" ) ) {
                                enable_push = false;
                                DEBUGV1 ( "CheckConfigFile: Disabled push socket." );
                            }
                        }
                    }

                    else if ( chars [ 0 ] == '2' && chars [ 1 ] == ' ' ) {
                        // We have a group
                        int idname; string optname; int value;

                        if ( iss >> idname >> optname >> value ) {
                            if ( !optname.compare ( "fport" ) ) {
                                if ( value > 0 && value < 66000 ) {
                                    fhem_port = value;
                                    DEBUGV1 ( "CheckConfigFile: Using fhem port " << value );
                                }
                            }
                            else if ( !optname.compare ( "pport" ) ) {
                                if ( value > 0 && value < 66000 ) {
                                    push_port = value;
                                    DEBUGV1 ( "CheckConfigFile: Using push port " << value );
                                }
                            }
                        }
                    }

                    else if ( chars [ 0 ] == '1' && chars [ 1 ] == ' ' ) {
                        // We have a group
                        int idname; string fhemname; string dename;

                        if ( iss >> idname >> fhemname >> dename ) {
                            // Add to groups
                            AddPushGroup ( dename.c_str (), fhemname.c_str () );
                        }
                    }

                    else {
                        uint64_t mac; string name;

                        if ( iss >> mac >> name ) {
                            //if ( ourmac == mac ) {
                            //	raspBee.name = name;
                            //}
                            //else {
                                AddPushDevice ( mac, name.c_str () );
                            //}
                        }
                    }
                }

                if ( pthread_mutex_unlock ( &groupLock ) ) {
                    DEBUGV ( "CheckConfigFile: Failed to unlock groupLock." );
                }
            }

            if ( pthread_mutex_unlock ( &devicesLock ) ) {
                DEBUGV ( "CheckConfigFile: Failed to unlock devicesLock." );
            }
        }
        mapFile.close ();
    }   
}


////////////////////////////////////////////////////////////////////////////////
// PushBridge Constructor
////////////////////////////////////////////////////////////////////////////////

PushBridge::PushBridge ()
{
    DEBUGL ( signal ( SIGSEGV, sigseghandler ); )

    apsInst = ApsController::instance ();
    DBG_Assert ( apsInst != 0 );

    DEBUGL ( logfile.open ( "/tmp/raspBeeBridge.log", std::ios_base::app ) );

    DEBUGV ( "PushBridge::Construct " );

    DEBUGL ( logfile1.open ( "/tmp/beeBridge.log", std::ios_base::app ) );

    DEBUGV1 ( "PushBridge::Construct " );

    devices.clear ();
    groups.clear ();

    pushLights.clear ();
    pushSensors.clear ();
    pushGroups.clear ();

#ifdef ENABLE_REACHABLE_TIMER
	devicesTimer.clear ();
	timerCurrent		= time ( 0 );
#endif

    raspBee.available   = true;
    raspBee.type        = PUSH_TYPE_CONTROLLER;

    fhemActive          = false;
    acceptActive        = false;
    pushActive          = false;

    // Init threads
    fhemSocket          = -1;
    pushSocket          = -1;

    memset ( &fhemThread, 0, sizeof (fhemThread) );
    memset ( &acceptThread, 0, sizeof (acceptThread) );
    memset ( &pushThread, 0, sizeof (pushThread) );

    DEBUGV1 ( "PushBridge::Construct: Initializing thread resources." );

    pthread_mutex_init ( &fhemQueueLock,	0 );
    pthread_mutex_init ( &devicesLock,		0 );
    pthread_mutex_init ( &groupLock,		0 );

    pthread_cond_init ( &fhemSignal,        NULL );

    pthread_mutex_init ( &pushLock,	        0 );
    pthread_cond_init ( &pushSignal,        NULL );

#ifdef ENABLE_REACHABLE_TIMER
    alive_timer = 0;
#endif
    
	// Update our own node
	//uint64_t ourmac	= apsInst->getParameter ( ParamMacAddress );
	//raspBee.mac		= ourmac;

    //DEBUGV1 ( "PushBridge::Construct: Our mac:" << ourmac );

    DEBUGV1 ( "PushBridge::Construct: Loading configuration." );

    configRequested = 0;
    lastConfigRequest = 0;
    CheckConfigFile ();

    if ( !enable_plugin ) {
        return;
    }

    //connect ( apsInst, SIGNAL ( apsdeDataConfirm ( const deCONZ::ApsDataConfirm & ) ),
    //        this, SLOT ( apsdeDataConfirm ( const deCONZ::ApsDataConfirm & ) ) );

    connect ( apsInst, SIGNAL ( apsdeDataIndication ( const deCONZ::ApsDataIndication & ) ),
            this, SLOT ( apsdeDataIndication ( const deCONZ::ApsDataIndication & ) ) );

    connect ( apsInst, SIGNAL ( nodeEvent ( deCONZ::NodeEvent ) ),
            this, SLOT ( nodeEvent ( deCONZ::NodeEvent ) ) );

    if ( enable_push ) {
        acceptThreadRun = true;

        DEBUGV1 ( "PushBridge::Construct: Starting accept thread." );

        pthread_create ( &acceptThread, 0, AcceptThreadStarter, this );

        pushThreadRun = true;

        //DEBUGV1 ( "PushBridge::Construct: Starting push thread." );

        //pthread_create ( &pushThread, 0, PushThreadStarter, this );
    }

    if ( enable_fhem_tunnel ) {
        fhemThreadRun = true;

        DEBUGV1 ( "PushBridge::Construct: Starting thread." );

        pthread_create ( &fhemThread, 0, FhemThreadStarter, this );
    }

#ifdef ENABLE_REACHABLE_TIMER
    // Timer to check the alive status of nodes
    alive_timer = new QTimer ( this );
    alive_timer->setSingleShot ( false );

    connect ( alive_timer, SIGNAL ( timeout () ), this, SLOT ( TimerAlive () ) );

    DEBUGV1 ( "PushBridge::Construct: Starting reachable check timer." );

    alive_timer->start ( 10000 );
#endif
}


////////////////////////////////////////////////////////////////////////////////
// PushBridge deconstructor
////////////////////////////////////////////////////////////////////////////////

PushBridge::~PushBridge()
{
    DEBUGV ( "PushBridge::Destruct " );
    DEBUGV1 ( "PushBridge::Destruct " );

    DEBUGL ( logfile.close() );
    DEBUGL ( logfile1.close() );
    
    std::map < uint64_t, PushDevice * >::iterator it = devices.begin ();
    
    while ( it != devices.end () ) {
        PushDevice * device = it->second;
        delete device;
        ++it;
    }

    devices.clear ();

#ifdef ENABLE_REACHABLE_TIMER
	devicesTimer.clear ();
#endif
    
    std::map < std::string, PushGroup * >::iterator itg = groups.begin ();
    
    while ( itg != groups.end () ) {
        PushGroup * group = itg->second;
        delete group;
        ++itg;
    }

    groups.clear ();

    fhemThreadRun = false;

    SignalFhemThread ();

    // Wait for thread exit
    pthread_join ( fhemThread, 0 );
    
    EmptyFhemQueue ();

    pthread_cond_destroy ( &fhemSignal );

    pthread_mutex_destroy ( &fhemQueueLock );
    pthread_mutex_destroy ( &groupLock );
	pthread_mutex_destroy ( &devicesLock );

    if ( fhemSocket != -1 ) {
        ::close ( fhemSocket );
        fhemSocket = -1;
    }

    pushThreadRun = false;
    acceptThreadRun = false;

    if ( acceptSocket != -1 ) {
        ::close ( acceptSocket );
        acceptSocket = -1;
    }

    if ( pushSocket != -1 ) {
        ::close ( pushSocket );
        pushSocket = -1;
    }

    SignalPushThread ();

    // Wait for thread exit
    //pthread_join ( pushThread, 0 );

    pthread_join ( acceptThread, 0 );

    pthread_cond_destroy ( &pushSignal );

    pthread_mutex_destroy ( &pushLock );
    
}


////////////////////////////////////////////////////////////////////////////////
// FHEM channel injection
////////////////////////////////////////////////////////////////////////////////

PushDevice * PushBridge::GetNodeDevice ( void * voidNode, int type )
{
    DEBUGFTRACE ( "GetNodeDevice" );

    PushDevice * device = 0;

    switch ( type )
    {
        case 0: device = ( ( RestNodeBase * ) voidNode )->m_pushDevice; break;
        case 1: return ( ( SensorState * ) voidNode )->m_pushDevice;
        case 2: return ( ( SensorConfig * ) voidNode )->m_pushDevice;
        default: return 0;
    }
    if ( !device )
        return 0;
    DEBUGFTRACE ( "GetNodeDevice 1" );

    //DEBUGFTRACE ( "GetNodeDevice 1:" << device->type );
    //DEBUGFTRACE ( "GetNodeDevice 1:" << device->name.c_str () );

	if ( device->needsRestUpdate ) 
    {
        device->type = ( ( RestNodeBase * ) voidNode )->m_pushType;

        DEBUGFTRACE ( "GetNodeDevice 2" );

        ( ( RestNodeBase * ) voidNode )->UpdateToFhem ( device );
	}

    DEBUGFTRACE ( "GetNodeDevice 3" );
    return device;
}


void PushBridge::SetNode ( void * derestNode, void * denode )
{
    DEBUGFTRACE ( "SetNode" );

    if ( !denode || !derestNode || !PushBridge::enable_plugin || !PushBridge::fhemActive )
        return;

    Node * node = ( Node * ) denode;
	
    uint64_t addr 	= node->address ().ext ();

    PushDevice * device = pushBridge.GetPushDevice ( addr );
    if ( device ) {
        RestNodeBase * restnode = ( RestNodeBase * ) derestNode;

        device->node = node;
        device->type = restnode->m_pushType;    		
        restnode->m_pushDevice = device;
    }
}


void PushBridge::UpdatePushNode ( void * restNode, int type )
{
    DEBUGFTRACE ( "UpdatePushNode" );

    int id = -1;
    int pushType = PUSH_TYPE_UNKNOWN;

    if ( type == 0 )
    {
        RestNodeBase * node = ( RestNodeBase * ) restNode;

        id = node->id ().toInt ();

        pushType = node->m_pushType;
    }
    else if ( type == 1 )
    {
        SensorState * node = ( SensorState * ) restNode;

        id = node->m_pushId;

        pushType = PUSH_TYPE_SWITCH;
    }
    else if ( type == 2 )
    {
        SensorConfig * node = ( SensorConfig * ) restNode;

        id = node->m_pushId;

        pushType = PUSH_TYPE_SWITCH;
    }

    if ( id >= 0 && !pthread_mutex_lock ( &pushLock ) )
    {
        if ( pushType == PUSH_TYPE_SWITCH ) {
            pushSensors [ id ] = id;      
        }
        else {  
            pushLights [ id ] = id;
        }

        if ( pthread_cond_broadcast ( &pushSignal ) ) {
            DEBUGV ( "UpdatePushNode: Failed to signal." );
        }

        if ( pthread_mutex_unlock ( &pushLock ) ) {
            DEBUGV ( "UpdatePushNode: Failed to unlock." );
        }
    }
}


void PushBridge::UpdatePushGroup ( QString & m_id )
{
    DEBUGFTRACE ( "UpdatePushGroup" );

    int id = m_id.toInt ();

    if ( !pthread_mutex_lock ( &pushLock ) )
    {
        pushGroups [ id ] = id;

        if ( pthread_cond_broadcast ( &pushSignal ) ) {
	        DEBUGV ( "UpdatePushGroup: Failed to signal." );
        }

        if ( pthread_mutex_unlock ( &pushLock ) ) {
	        DEBUGV ( "UpdatePushGroup: Failed to unlock." );
        }
    }
}


void PushBridge::SetNodeAvailable ( void * restNode, bool available )
{
    DEBUGFTRACE ( "SetNodeAvailable" );
    if ( !restNode )
        return;

    if ( acceptActive ) {
        UpdatePushNode ( restNode, 0 );
    }

    if ( !PushBridge::fhemActive ) {
        return;
    }

    PushDevice * device = GetNodeDevice ( restNode, 0 );
    if ( !device )
        return;
        
    if ( __sync_val_compare_and_swap ( & device->available, !available, available ) == !available ) 
    {
        int reachable = ( int ) available;

        DEBUGV2 ( "SetNodeAvailable: Reachable " << reachable << " to mac " << device->mac );

        char cmd [ 256 ];
        const char * name = device->name.c_str ();

        DEBUGV ( "SetNodeAvailable: Reachable " << reachable << " to device:" << name );
        DEBUGV1 ( "SetNodeAvailable: Reachable " << reachable << " to device:" << name );

        snprintf ( cmd, 256, "setreading %s reachable %i\nsetreading %s _reachable %i\n"
            "setreading %s available %i\n"
        , name, reachable, name, reachable, name, reachable );

        EnqueueToFhem ( cmd );
    }
}


void PushBridge::SetNodeInfo ( void * restNode, int type, const char * reading, const QString & value )
{
    DEBUGFTRACE ( "SetNodeInfo 1:" << reading );
    if ( !restNode )
        return;

    if ( acceptActive ) {
        UpdatePushNode ( restNode, type );
    }

    if ( !PushBridge::fhemActive ) {
        return;
    }

    PushDevice * device = GetNodeDevice ( restNode, type );
    if ( !device )
        return;
    DEBUGFTRACE ( "SetNodeInfo 11" );
    const char * name = device->name.c_str ();
    
    DEBUGFTRACE ( "SetNodeInfo 111" );
    if ( !name ) {
        DEBUGFTRACE ( "SetNodeInfo 13: Invalid name!" );
        return;
    }
    DEBUGV ( "SetNodeInfo " << GetNodeType ( device->type ) << ": " << name << " " << reading );

    char buffer [ 256 ];
    snprintf ( buffer, 256, "setreading %s %s %s\n", name, reading, qPrintable ( value ) );

    DEBUGFTRACE ( "SetNodeInfo 15" );
    EnqueueToFhem ( buffer );
}


void PushBridge::SetNodeInfo ( void *, QString & qdevice, const char * reading, uint32_t value )
{
    DEBUGFTRACE ( "SetNodeInfo 2" );
    if ( qdevice.length () <= 0 )
        return;
    
    const char * name = qPrintable ( qdevice );
    
    DEBUGV ( "SetNodeInfo: " << name << " " << reading );

    char buffer [ 256 ];
    snprintf ( buffer, 256, "setreading %s %s %i\n", name, reading, value );

    EnqueueToFhem ( buffer );
}


void PushBridge::SetNodeInfo ( void * restNode, int type, const char * reading, QString & value ) {
    SetNodeInfo ( restNode, type, reading, (const QString &) value );
}


void PushBridge::SetNodeInfo ( void * restNode, int type, const char * reading, uint32_t value )
{
    DEBUGFTRACE ( "SetNodeInfo 3: " << reading << " v:" << value );

    if ( !restNode )
        return;

    if ( acceptActive ) {
        UpdatePushNode ( restNode, type );
    }

    if ( !PushBridge::fhemActive ) {
        return;
    }

    PushDevice * device = GetNodeDevice ( restNode, type );
    if ( !device )
        return;
    const char * name = device->name.c_str ();
    
    DEBUGV ( "SetNodeInfo " << GetNodeType ( device->type ) << ": " << name << " " << reading );

    char buffer [ 256 ];
    snprintf ( buffer, 256, "setreading %s %s %i\n", name, reading, value );

    EnqueueToFhem ( buffer );
}


void PushBridge::SetNodeInfoDouble ( void * restNode, int type, const char * reading, double value )
{
    DEBUGFTRACE ( "SetNodeInfoDouble" );
    if ( !restNode )
        return;

    if ( acceptActive ) {
        UpdatePushNode ( restNode, type );
    }

    if ( !PushBridge::fhemActive ) {
        return;
    }

    PushDevice * device = GetNodeDevice ( restNode, type );
    if ( !device )
        return;
    const char * name = device->name.c_str ();
    
    DEBUGFTRACE ( "SetNodeInfoDouble " << GetNodeType ( device->type ) << ": " << name << " " << reading );

    char buffer [ 256 ];
    snprintf ( buffer, 256, "setreading %s %s %f\n", name, reading, value );

    EnqueueToFhem ( buffer );
}


void PushBridge::SetNodeState ( void * restNode, int type, bool reading )
{
    DEBUGFTRACE ( "SetNodeState" );
    if ( !restNode )
        return;

    if ( acceptActive ) {
        UpdatePushNode ( restNode, type );
    }

    if ( !PushBridge::fhemActive ) {
        return;
    }

    PushDevice * device = GetNodeDevice ( restNode, type );
    if ( !device )
        return;
    const char * name = device->name.c_str ();
    
    DEBUGFTRACE ( "SetNodeState " << GetNodeType ( device->type ) << ": " << name << " " << reading );

    char buffer [ 256 ];
    snprintf ( buffer, 256, "setreading %s state %s\nsetreading %s onoff %d\n", name, reading ? "on" : "off", name, reading ? 1 : 0 );

    EnqueueToFhem ( buffer );
}


void PushBridge::SetGroupInfo ( QString & groupName, QString & id, const char * reading, uint32_t value )
{
    DEBUGV2 ( "SetGroupInfo 1" );

    if ( fhemActive ) {
        PushGroup * group = GetPushGroup ( groupName );
        if ( !group  ) {
            return;
        }

        SetGroupInfo ( group->name.c_str (), id, reading, value );
    }
    else if ( acceptActive  ) {
        UpdatePushGroup ( id );
    }
}


void PushBridge::SetGroupInfo ( const char * groupName, QString & id, const char * reading, uint32_t value )
{
    DEBUGV ( "SetGroupInfo 2: " << groupName << " " << reading );

    if ( fhemActive ) {
        char buffer [ 128 ];
        snprintf ( buffer, 128, "setreading %s %s %i\n", groupName, reading, value );

        EnqueueToFhem ( buffer );
    }

    if ( acceptActive  ) {
        UpdatePushGroup ( id );
    }
}


void PushBridge::SetGroupInfo ( const char * groupName, QString & id, const char * reading, float value )
{
    DEBUGFTRACE ( "SetGroupInfo: " << groupName << " " << reading );

    if ( fhemActive ) {
        char buffer [ 128 ];
        snprintf ( buffer, 128, "setreading %s %s %f\n", groupName, reading, value );

        EnqueueToFhem ( buffer );
    }

    if ( acceptActive  ) {
        UpdatePushGroup ( id );
    }
}


PushGroup * PushBridge::GetPushGroup ( QString & groupName, bool checkCfg )
{
    DEBUGFTRACE ( "GetPushGroup" );
	if ( groupName.length () <= 0 )
		return 0;

	PushGroup	* group		= 0;
	const char	* dename	= qPrintable ( groupName );

	if ( !pthread_mutex_lock ( &groupLock ) )
	{
		map < std::string, PushGroup * >::iterator it = groups.find ( dename );
		if ( it != groups.end () )
		{
			group = it->second;
		}
		else {
			DEBUGV ( "GetPushGroup: fhemname for " << dename << " NOT found." );
		}

		if ( pthread_mutex_unlock ( &groupLock ) ) {
			DEBUGV ( "GetPushGroup: Failed to unlock." );
		}
		
		if ( !group && checkCfg ) {
            RequestConfigFile ();

			CheckConfigFile ();
			
			group = GetPushGroup ( groupName, false );
		}
	}
    
    return group;
}


void PushGroup::Update ( QString & id, bool o, uint16_t x, uint16_t y, uint16_t h, float hf, uint16_t s, uint16_t l, uint16_t temp )
{
    if ( !pushBridge.fhemActive )
        return;

    DEBUGFTRACE ( "PushGroup::Update" );
    
    const char * groupName = name.c_str ();

    if ( on != o ) {
        on = o;
    //    pushBridge.SetGroupInfo ( groupName, id, "on", ( uint32_t ) ( o ? 1 : 0 ) );
    }

    if ( colorX != x ) {
        colorX = x;
        pushBridge.SetGroupInfo ( groupName, id, "colorX", ( uint32_t ) x );
    }

    if ( colorY != y ) {
        colorY = y;
        pushBridge.SetGroupInfo ( groupName, id, "colorY", ( uint32_t ) y );
    }

    if ( hue != h ) {
        hue = h;
        pushBridge.SetGroupInfo ( groupName, id, "hue", ( uint32_t ) h );
    }

    if ( sat != s ) {
        sat = s;
        pushBridge.SetGroupInfo ( groupName, id, "sat", ( uint32_t ) s );
    }

    if ( level != l ) {
        level = l;
        pushBridge.SetGroupInfo ( groupName, id, "level", ( uint32_t ) l );
    }

    if ( colorTemperature != temp ) {
        colorTemperature = temp;
        pushBridge.SetGroupInfo ( groupName, id, "colorTemperature", ( uint32_t ) temp );
    }

    if ( hueFloat != hf ) {
        hueFloat = hf;
        pushBridge.SetGroupInfo ( groupName, id, "hueReal", hf );
    }
}


////////////////////////////////////////////////////////////////////////////////
// FHEM channel thread and management
////////////////////////////////////////////////////////////////////////////////

void PushBridge::SignalFhemThread ()
{
    DEBUGFTRACE ( "SignalFhemThread" );
    if ( !pthread_mutex_lock ( &fhemQueueLock ) )
    {
        if ( pthread_cond_broadcast ( &fhemSignal ) ) {
	        DEBUGV ( "SignalFhemThread: Failed to signal." );
        }

        if ( pthread_mutex_unlock ( &fhemQueueLock ) ) {
	        DEBUGV ( "SignalFhemThread: Failed to unlock." );
        }
    }
}


void PushBridge::EmptyFhemQueue ()
{
    DEBUGFTRACE ( "EmptyFhemQueue" );
    if ( !pthread_mutex_lock ( &fhemQueueLock ) )
    {
        while ( !fhemQueue.empty () ) {
            char * cmd = fhemQueue.front ();
            fhemQueue.pop ();
            free ( cmd );
        }

        if ( pthread_mutex_unlock ( &fhemQueueLock ) ) {
	        DEBUGV ( "EmptyFhemQueue: Failed to unlock." );
        }
    }
}


void PushBridge::EnqueueToFhem ( const char * cmd )
{
    DEBUGFTRACE ( "EnqueueToFhem" );
    if ( !enable_fhem_tunnel )
        return;
        
    DEBUGFTRACE ( "EnqueueToFhem:" );

    if ( !cmd )
        return;

    char * item = strdup ( cmd );
    if ( !item )
        return;

    DEBUGFTRACE ( "EnqueueToFhem: " << cmd );

    if ( !pthread_mutex_lock ( &fhemQueueLock ) )
    {
        fhemQueue.push ( item );

        if ( pthread_cond_broadcast ( &fhemSignal ) ) {
	        DEBUGV ( "EnqueueToFhem: Failed to signal." );
        }

        if ( pthread_mutex_unlock ( &fhemQueueLock ) ) {
	        DEBUGV ( "EnqueueToFhem: Failed to unlock." );
        }
    }
    else {
	    DEBUGV ( "EnqueueToFhem: Failed to lock." );

        free ( item );
    }

    DEBUGFTRACE ( "EnqueueToFhem: done." );
}


void PushBridge::SendToFhem ( const char * cmd )
{
    DEBUGFTRACE ( "SendToFhem:" << cmd );

    bool retried = false;

Retry:
    if ( fhemSocket < 0 ) {
        // Create socket
        DEBUGV ( "PushBridge::SendToFhem: Creating socket ..." );

        int sock = ( int ) socket ( PF_INET, SOCK_STREAM, 0 );
        if ( sock < 0 ) {
            DEBUGV ( "PushBridge::SendToFhem: Failed to create socket." );

            EmptyFhemQueue ();
            return;
        }
        
		struct sockaddr_in  addr;
        memset ( &addr, 0, sizeof ( addr ) );

		addr.sin_family		    = PF_INET;
		addr.sin_port		    = htons ( fhem_port );
		addr.sin_addr.s_addr	= htonl ( INADDR_LOOPBACK );

        DEBUGV ( "PushBridge::SendToFhem: Connecting to FHEM ..." );

        int rc = ::connect ( sock, (struct sockaddr *) & addr, sizeof ( struct sockaddr_in ) );
        if ( rc < 0 ) {
            DEBUGV ( "PushBridge::SendToFhem: Failed to connect to FHEM." );

            EmptyFhemQueue ();

            ::close ( sock );
            return;
        }
        else {
            fhemSocket = sock;
        }
    }
    
    size_t len = strlen ( cmd );

    DEBUGFTRACE ( "PushBridge::SendToFhem: send to FHEM ..." );

    int bytesSent = ( int ) ::send ( fhemSocket, cmd, len, MSG_NOSIGNAL );
    if ( bytesSent != ( int ) len ) {
        DEBUGV ( "PushBridge::SendToFhem: Failed to send to FHEM." );

        ::close ( fhemSocket );
        fhemSocket = -1;

        if ( !retried ) {
            retried = true;
            goto Retry;
        }
        
        EmptyFhemQueue ();
    }
    else {
        DEBUGV ( "SendToFhem: " << cmd );

        char buffer [ 128 ];
        *buffer = 0;

        int rc = recv ( fhemSocket, buffer, 127, MSG_DONTWAIT );
        if ( rc > 0 ) {
            if ( rc > 127 )
                rc = 127;
            buffer [ rc ] = 0;
            DEBUGV ( "SendToFhem: Response: " << cmd );

            if ( strstr ( buffer, "Please define" ) ) {
                DEBUGV ( "SendToFhem: Please define found." );
                CheckConfigFile ();
            }
        }
    }
}


void PushBridge::FhemThread ()
{
    fhemActive = true;

    DEBUGV ( "FhemThread started ..." );

    bool doUnlock = true;
    
    // Request creation of config file
    RequestConfigFile ();

    if ( !pthread_mutex_lock ( &fhemQueueLock ) )
    {
        while ( fhemThreadRun )
        {
            char * cmd = 0;

            if ( !fhemQueue.empty () ) {
                cmd = fhemQueue.front ();
                fhemQueue.pop ();
            }

            if ( cmd ) {
                if ( pthread_mutex_unlock ( &fhemQueueLock ) ) {
                    DEBUGV ( "FhemThread: Failed to unlock." );
                    doUnlock = false; break;
                }

                SendToFhem ( cmd );
                free ( cmd );

                if ( pthread_mutex_lock ( &fhemQueueLock ) ) {
                    DEBUGV ( "FhemThread: Failed to lock." );
                    doUnlock = false; break;
                }
                continue;
            }

            if ( pthread_cond_wait ( &fhemSignal, &fhemQueueLock ) ) {
                DEBUGV ( "FhemThread: Failed to wait for signal." );
                doUnlock = false; break;
            }

            DEBUGV2 ( "FhemThread signaled." );
        }

        if ( doUnlock && pthread_mutex_unlock ( &fhemQueueLock ) ) {
            DEBUGV ( "FhemThread: Failed to unlock." );
        }
    }

    fhemActive = false;

    DEBUGV ( "FhemThread bye bye." );
}


void * PushBridge::FhemThreadStarter ( void * arg )
{
    ((PushBridge *) arg)->FhemThread ();
    return 0;
}


void PushBridge::AcceptThread ()
{
    acceptActive = true;

    DEBUGV ( "AcceptThread started ..." );

    while ( acceptThreadRun )
    {
        if ( acceptSocket != -1 ) {
            ::close ( acceptSocket );        
        }

        acceptSocket = ( int ) socket ( PF_INET, SOCK_STREAM, 0 );
        if ( acceptSocket < 0 ) {
            DEBUGV ( "PushBridge::AcceptThread: Failed to create socket." );
            break;
        }

        struct sockaddr_in  addr;
        memset ( &addr, 0, sizeof ( addr ) );

        addr.sin_family		    = PF_INET;
        addr.sin_port		    = htons ( push_port );
        addr.sin_addr.s_addr	= htonl ( INADDR_LOOPBACK );
        
        int ret = ::bind ( acceptSocket, ( struct sockaddr * )&addr, sizeof ( addr ) );
        if ( ret < 0 ) {
            DEBUGV ( "PushBridge::AcceptThread: Failed to bind socket." );
            break;
        }
        
        ret = listen ( acceptSocket, SOMAXCONN );
        if ( ret < 0 ) {
            DEBUGV ( "PushBridge::AcceptThread: Failed to listen on socket." );
            break;
        }
        
        while ( acceptThreadRun )
        {
            struct 	sockaddr_in		aaddr;
            socklen_t 				addrLen = sizeof ( aaddr );
            
            memset ( &aaddr, 0, sizeof ( aaddr ) );

            int sock = ( int ) accept ( acceptSocket, ( struct sockaddr * ) &aaddr, &addrLen );

            if ( sock < 0 ) {
                DEBUGV ( "PushBridge::AcceptThread: Socket has been closed." );
                break;
            }

            if ( pushSocket != -1 ) {
                ::close ( pushSocket );        
            }

            pushSocket = sock;

            PushThread ();
        }

        break;
    }
        

    acceptActive = false;

    DEBUGV ( "AcceptThread bye bye." );
}


void * PushBridge::AcceptThreadStarter ( void * arg )
{
    ((PushBridge *) arg)->AcceptThread ();
    return 0;
}


void PushBridge::SignalPushThread ()
{
    DEBUGFTRACE ( "SignalPushThread" );
    if ( !pthread_mutex_lock ( &pushLock ) )
    {
        if ( pthread_cond_broadcast ( &pushSignal ) ) {
	        DEBUGV ( "SignalPushThread: Failed to signal." );
        }

        if ( pthread_mutex_unlock ( &pushLock ) ) {
	        DEBUGV ( "SignalPushThread: Failed to unlock." );
        }
    }
}


void CollectIDs ( char * buffer, const char * type, std::map < int, int > & ids, size_t & len, int & remain )
{
    if ( ids.size () > 0 ) {
        DEBUGFTRACE ( "CollectIDs: Collecting " << ids.size () << " " << type << " ..." );

        int chars = snprintf ( buffer + len, remain, type );
        if ( chars > 0 ) 
        {
            len += chars; remain -= len;

            std::map < int, int >::iterator it = ids.begin ();
            
            while ( it != ids.end () ) {
                chars = snprintf ( buffer + len, remain, "%i,", it->first );
                if ( chars <= 0 )
                    break;
                len += chars; remain -= len;

                ++it;
            }

            len--;

            chars = snprintf ( buffer + len, remain, "\n" );
            if ( chars > 0 ) {
                len += chars; remain -= len;
            }
        }
        ids.clear ();
    }
}

void PushBridge::PushThread ()
{
    pushActive = true;

    DEBUGV ( "PushThread started ..." );

    char    buffer [ 1024 ];
    size_t  len     = 0;
    bool    wait    = false;

    while ( pushThreadRun && pushSocket != -1 )
    {
        if ( pthread_mutex_lock ( &pushLock ) ) {
            DEBUGV ( "PushThread: Failed to lock." );
            break;
        }

        if ( wait ) {
            if ( pthread_cond_wait ( &pushSignal, &pushLock ) ) {
                DEBUGV ( "PushThread: Failed to wait for signal." );
                break;
            }         
        }

        DEBUGV2 ( "PushThread signaled." );

        if ( pushSocket == -1 ) {
            if ( pthread_mutex_unlock ( &pushLock ) ) {
                DEBUGV ( "PushThread: Failed to lock." );
            }
            break;
        }

        *buffer = 0;
        
        int remain  = 1024;
        len     = 0;

        CollectIDs ( buffer, "l: ", pushLights, len, remain );

        CollectIDs ( buffer, "s: ", pushSensors, len, remain );

        CollectIDs ( buffer, "g: ", pushGroups, len, remain );

        if ( pthread_mutex_unlock ( &pushLock ) ) {
            DEBUGV ( "PushThread: Failed to lock." );
            break;
        }

        if ( len > 0 ) {
            DEBUGV ( "PushThread: " << buffer );

            int rc = ::send ( pushSocket, buffer, len, MSG_NOSIGNAL );
            if ( rc > 0 ) {
                rc = ::recv ( pushSocket, buffer, 10, 0 );
                if ( rc > 0 && buffer [ 0 ] == '1' ) {
                    wait = false;   
                    continue;
                }
            }

            ::close ( pushSocket );
            pushSocket = -1;
            break;
        }
        else {
            wait = true;
        }
    }

    pushActive = false;

    DEBUGV ( "PushThread bye bye." );
}


void * PushBridge::PushThreadStarter ( void * arg )
{
    ((PushBridge *) arg)->PushThread ();
    return 0;
}


#ifdef DEBUG

////////////////////////////////////////////////////////////////////////////////
// Debug helper methods
////////////////////////////////////////////////////////////////////////////////

const char * GetNodeType ( int type )
{
	if ( type == PUSH_TYPE_CONTROLLER ) return "C";
	if ( type == PUSH_TYPE_PLUG ) return "P";
	if ( type == PUSH_TYPE_LIGHT ) return "L";
	if ( type == PUSH_TYPE_SWITCH ) return "S";
	if ( type == PUSH_TYPE_MOTION ) return "M";
	return "U";
}


size_t FormatTimeString ( char * timeBuffer, unsigned int bufferSize )
{
    time_t now;
    struct tm timeInfo;

    time ( &now );
    if ( !localtime_r ( &now, &timeInfo ) ) {
        timeBuffer [ 0 ] = 'e';
        timeBuffer [ 1 ] = 0;
        return 1;
    }

    return strftime ( timeBuffer, bufferSize, "%a %b %d %H:%M:%S: ", &timeInfo );
}


const char * GetTimeString ()
{
    time_t now;
    struct tm timeInfo;

    static char timeBuffer [ 64 ] =  { 0 };

    time ( &now );
    if ( !localtime_r ( &now, &timeInfo ) ) {
        timeBuffer [ 0 ] = 'e';
        timeBuffer [ 1 ] = 0;
    }
    else {
        strftime ( timeBuffer, 64, "%a %b %d %H:%M:%S: ", &timeInfo );
    }
    return timeBuffer;
}


const char * ConvertToHexSpaceString ( const char * src, unsigned int length )
{
    DEBUGFTRACE ( "ConvertToHexSpaceString" );

	if ( !src || !length )
		return 0;

	static char buffer [ 4096 ];
    *buffer = 0;

	for ( unsigned int i = 0; i < length; i++ )
	{
		sprintf ( buffer + ( i * 3 ), "%02X ", ( unsigned char ) src [ i ] );
	}

	return buffer;
}


const char * eventNames [ ] = {
        "NodeSelected",
        "NodeDeselected",
        "NodeAdded",
        "NodeRemoved",
        "NodeZombieChanged",
        "UpdatedNodeDescriptor",
        "UpdatedPowerDescriptor",
        "UpdatedUserDescriptor",
        "UpdatedSimpleDescriptor",
        "UpdatedClusterData",
        "UpdatedClusterDataZclRead",
        "UpdatedClusterDataZclReport"
};

#define MAX_EVENT_NAME  (sizeof(eventNames)/sizeof(eventNames[0]))

#endif


////////////////////////////////////////////////////////////////////////////////
// PushDevice management
////////////////////////////////////////////////////////////////////////////////

PushDevice::PushDevice ( uint64_t deviceMac, const char * deviceName ) {
    SetIdentity ( deviceMac, deviceName );
}


PushDevice::~PushDevice () {}


void PushDevice::SetIdentity ( uint64_t deviceMac, const char * deviceName )
{
    DEBUGFTRACE ( "PushDevice::SetIdentity" );
    
    available   = false;

#ifdef ENABLE_REACHABLE_TIMER
    lastMS  = 0;
    lastTS  = time ( 0 );
#endif

    node    = 0;
    mac     = deviceMac;
    type    = PUSH_TYPE_UNKNOWN;

    needsNodeUpdate = true;
    needsRestUpdate = true;

    if ( !deviceName || strlen ( deviceName ) <= 0 ) {
        name = UNKNOWN_DEVICE_NAME;
    }
    else {
        name = deviceName;
    }
}


const char * PushBridge::GetPushName ( uint64_t mac )
{
    DEBUGFTRACE ( "GetPushName: mac " << mac );

    PushDevice * device = GetPushDevice ( mac );
    if ( device ) {
        return device->name.c_str ();
    }
    return UNKNOWN_DEVICE_NAME;
}


PushDevice * PushBridge::GetPushDevice ( uint64_t mac, bool update, bool checkCfg )
{
    DEBUGFTRACE ( "GetPushDevice: mac " << mac );

    if ( mac == raspBee.mac ) {
        DEBUGV2 ( "GetPushDevice: mac " << mac << " found raspBee." );
        return &raspBee;
    }
	
	PushDevice * device = 0;

	if ( !pthread_mutex_lock ( &devicesLock ) )
	{
		map < uint64_t, PushDevice * >::iterator it = devices.find ( mac );
		if ( it != devices.end () ) {
			device = it->second;
		}

		if ( pthread_mutex_unlock ( &devicesLock ) ) {
			DEBUGV ( "GetPushDevice: Failed to unlock." );
		}

		if ( device ) {
			DEBUGV2 ( "GetPushDevice: mac " << mac << " found. type:" << device->type );

#ifdef ENABLE_REACHABLE_TIMER
			uint64_t curTS = time ( 0 );
#endif
            if ( update && ( __sync_val_compare_and_swap ( &device->available, false, true ) == false ) )
			{
#ifdef ENABLE_REACHABLE_TIMER
				uint64_t diff = curTS - device->lastTS;
				device->lastTS = curTS;
#endif
				//device->needsRestUpdate = true;

				if ( fhem_node_update ) {
					const Node * node = ( Node * ) device->node;
					if ( node ) {
						DEBUGV2 ( "GetPushDevice: Updating node. mac:" << device->mac << " name:" << device->name.c_str () );

						apsInst->updateNode ( *node );
					}
					else {
						DEBUGV ( "GetPushDevice: name:" << device->name.c_str () << " node is MISSING!" );
					}
				}

				char cmd [ 128 ];
				const char * name = device->name.c_str ();

#ifdef ENABLE_REACHABLE_TIMER
				DEBUGV2 ( "GetPushDevice: setreading reachable mac:" << mac << " or name:" << name << " ms:" << diff );
				DEBUGV ( "GetPushDevice: setreading reachable mac:" << mac << " or name:" << name << " ms:" << diff );
#else
				DEBUGV2 ( "GetPushDevice: setreading reachable mac:" << mac << " or name:" << name );
				DEBUGV ( "GetPushDevice: setreading reachable mac:" << mac << " or name:" << name );
#endif
				snprintf ( cmd, 128, "setreading %s reachable 1\nsetreading %s _reachable 1\n", name, name );
				EnqueueToFhem ( cmd );
			}
#ifdef ENABLE_REACHABLE_TIMER
			else {
				device->lastTS = curTS;
			}
#endif
		}
		else {
			DEBUGV ( "GetPushDevice: mac " << mac << " NOT found." );
		
            if ( checkCfg ) {
                RequestConfigFile ();

                CheckConfigFile ();
                
                device = GetPushDevice ( mac, true, false );
            }
		}
	}
    return device;
}


void PushBridge::UpdatePushDevice ( PushDevice * device )
{
    DEBUGFTRACE ( "UpdatePushDevice" );

    if ( !device )
        return;
    const char * name = device->name.c_str ();

    if ( device->needsNodeUpdate ) 
    {
        Node * node = ( Node * ) device->node;
        if ( node )
        {
            device->needsNodeUpdate = false;

            char cmd [ 512 ];

            snprintf ( cmd, 512,
                "setreading %s userDescriptor %s\n"
                "setreading %s deviceTypeString %s\n",
                name, qPrintable ( node->userDescriptor() ),
                name, qPrintable ( node->deviceTypeString() )
                );
            EnqueueToFhem ( cmd );

            const NodeDescriptor & desc = node->nodeDescriptor ();

            snprintf ( cmd, 512,"setreading %s manufacturerCode %i\nsetreading %s deviceType %i\n"
                "setreading %s macCapabilities %i\n"
                "setreading %s isFullFunctionDevice %i\n"
                "setreading %s isMainsPowered %i\n"
                "setreading %s receiverOnWhenIdle %i\n"
                "setreading %s securitySupport %i\n"
                "setreading %s frequencyBandString %s\n",
                name, desc.manufacturerCode(),
                name, (int) desc.deviceType(),
                name, (int) desc.macCapabilities(), 
                name, desc.isFullFunctionDevice() ? 1 : 0,
                name, desc.isMainsPowered() ? 1 : 0,
                name, desc.receiverOnWhenIdle() ? 1 : 0,
                name, desc.securitySupport() ? 1 : 0,
                name, desc.frequencyBandString()
                );
            EnqueueToFhem ( cmd );
        }
    }
}


bool PushBridge::AddPushDevice ( uint64_t mac, const char * name )
{
    DEBUGFTRACE ( "AddPushDevice: mac " << mac );

    if ( !mac || !name || strlen ( name ) <= 0 ) {
        DEBUGV1 ( "AddPushDevice: INVALID mac:" << mac << " or name" );
        return false;
    }

    PushDevice * device;

    map < uint64_t, PushDevice * >::iterator it = devices.find ( mac );
    if ( it != devices.end () ) 
    {
        device = it->second;
        DEBUGV1 ( "PushDevice updating the list mac:" << mac << " name:" << name );

        device->name = name;

        UpdatePushDevice ( device );
        return false;
    }

    device = new PushDevice ( mac, name );
    if ( !device ) {
        DEBUGV1 ( "Insufficient memory to create PushDevice mac:" << mac << " name:" << name );
        return false;
    }

    devices		 [ mac ] = device;

#ifdef ENABLE_REACHABLE_TIMER
	devicesTimer [ mac ] = device;
#endif

    DEBUGV1 ( "PushDevice ADDED mac:" << mac << " name:" << name );
    return true;
}


bool PushBridge::AddPushGroup ( const char * dename, const char * fhemname )
{
    DEBUGFTRACE ( "AddPushGroup" );

    if ( !dename || strlen ( dename ) <= 0 || !fhemname || strlen ( fhemname ) <= 0 ) {
        DEBUGV1 ( "AddPushGroup: INVALID fhemname or deconzname" );
        return false;
    }

    map < std::string, PushGroup * >::iterator it = groups.find ( dename );
    if ( it != groups.end () )
    {
        PushGroup * group = it->second;
        if ( group ) {
            group->name = fhemname;
        }
    }
    else {
        PushGroup * group = new PushGroup ( fhemname );
        if ( group ) {
            groups [ dename ] = group;
            
            DEBUGV1 ( "AddPushGroup: group:" << dename << " with fhemname:" << fhemname );
        }
    }
    return true;
}


bool PushBridge::UpdatePushDevice ( uint64_t mac, const Node * node )
{
    DEBUGFTRACE ( "UpdatePushDevice" );

    if ( !mac || !node ) {
        DEBUGV1 ( "INVALID mac:" << mac << " or node" );
        return false;
    }

	if ( raspBee.mac == mac ) {
		raspBee.node = node;
		return true;
	}

    DEBUGFTRACE ( "UpdatePushDevice: Searching ..." );

	PushDevice * device = 0;

	if ( !pthread_mutex_lock ( &devicesLock ) )
	{
		map < uint64_t, PushDevice * >::iterator it = devices.find ( mac );
		if ( it != devices.end () ) {
			device = it->second;
		}

		if ( pthread_mutex_unlock ( &devicesLock ) ) {
			DEBUGV ( "UpdatePushDevice: Failed to unlock." );
		}

		if ( device ) {
			DEBUGV2 ( "UpdatePushDevice: device " << mac << " found." );

			if ( device->node != node )
			{
				device->node    = node;

				DEBUGV1 ( "PushDevice updating the list with node, mac:" << mac << " name:" << device->name.c_str () );
			}
#ifdef DEBUG1
			else {
				if ( device->name.length () <= 0 ) {
					DEBUGV1 ( "ERROR: PushDevice already in the list and missing name! mac:" << mac );
				}
				else {
					DEBUGV1 ( "PushDevice already complete mac:" << mac << " name:" << device->name.c_str () );
				}
			}
#endif
			UpdatePushDevice ( device );
			return true;
		}
        DEBUGL ( else { DEBUGV2 ( "UpdatePushDevice: PushDevice NOT found! mac:" << mac ); } )
	}

    return false;
}


////////////////////////////////////////////////////////////////////////////////
// Reachable check APS API: DataIndication
////////////////////////////////////////////////////////////////////////////////

void PushBridge::apsdeDataIndication ( const deCONZ::ApsDataIndication & ind )
{
    DEBUGFTRACE ( "apsdeDataIndication" );

    DEBUGL ( const char * addrs = "0x0000" );

#ifdef DEBUGV3
    uint64_t addrd = ind.dstAddress ().ext ();
    if ( addrd ) {
        const char * addrdest = qPrintable ( ind.dstAddress ().toStringExt () );
        DEBUGV1 ( "apsdeDataIndication dst:" << addrdest );
    }
#endif

    uint64_t addr = ind.srcAddress ().ext ();
    if ( addr == 0 ) {
        addr = ind.dstAddress ().ext ();
        
        DEBUGL ( if ( addr ) { addrs = qPrintable ( ind.dstAddress ().toStringExt () ); } )
    }
    DEBUGL ( else { addrs = qPrintable ( ind.srcAddress ().toStringExt () ); } )
    // DEBUGV1 ( "apsdeDataIndication src:" << addrs ); 
    
#ifdef ENABLE_REACHABLE_TIMER
    DEBUGL ( int ms = -1 );
#endif
    const char * name = "Unknown";

    if ( !fhemActive )
        return;
        
    PushDevice * device = GetPushDevice ( addr );

    if ( device ) {
        name    = device->name.c_str ();
        
#ifdef ENABLE_REACHABLE_TIMER
        DEBUGL ( ms      = device->lastMS );
#endif
        const char * data = ind.asdu ().data ();
        if ( data ) 
        {
            int size = ind.asdu ().size ();

            if ( device->type == PUSH_TYPE_SWITCH ) 
            {
                DEBUGV1 ( "apsdeDataIndication: isSwitch 1 size:" << size << " " );
                //const char * hex = ConvertToHexSpaceString ( data, (unsigned) size );                
                //logfile1 << "\t" << hex << endl;

                if ( size >= 3 && data [ 0 ] == 0x1 ) {
                    DEBUGV10 ( "1 "; )
                    int btn = -1;
                    char btn1 = data [ 2 ];

                    if ( btn1 == 0x2 ) {
                        DEBUGV10 ( "2 "; )
                        if ( size >= 7 ) {
                            DEBUGV10 ( "3 "; )
                            btn1 = data [ 3 ];
                            DEBUGV10 ( "4 "; )
                            if ( btn1 == 0 ) {
                                btn = 2;
                            }
                            else if ( btn1 == 0x1 ) {
                                btn = 3;
                            }
                        }
                    }
                    else {
                        DEBUGV10 ( "20 "; )
                        //DEBUGV1 ( "apsdeDataIndication: isSwitch 7 " << (int) btn1 );
                        if ( btn1 == 0x1 ) {
                            if ( size == 3 ) btn = 1;
                        }
                        else if ( btn1 == 0x40 ) {
                            DEBUGV10 ( "30 "; )
                            if ( size == 5 && data [ 3 ] == 0 && data [ 4 ] == 0 ) {
                                DEBUGV10 ( "40 "; )
                                btn = 0;
                            }
                        }
                    }
                    DEBUGV10 ( endl );

                    if ( btn >= 0 ) {
                        DEBUGV ( "apsdeDataIndication: btn:" << btn );
                        DEBUGV2 ( "apsdeDataIndication: btn:" << btn );
                        int seq = ( int ) data [ 1 ];

                        char cmd [ 128 ];

                        snprintf ( cmd, 128, "setreading %s button%i %i\nsetreading %s buttonLast %i\n", name, btn, seq, name, btn );

                        EnqueueToFhem ( cmd );
                    }
#ifdef DEBUG
                    else {
                        DEBUGV1 ( "apsdeDataIndication: button unrecognized. size:" << size );

                        const char * hex = ConvertToHexSpaceString ( data, (unsigned) size );                
                        logfile1 << "\t" << hex << endl;
                    }
#endif                    
                }
            }
            else {
                if ( size > 0 ) {
                    if ( data [ 0 ] == 0 && device->type == PUSH_TYPE_LIGHT ) {
                        //device->needsRestUpdate = true;

                        if ( fhem_node_update ) {
                            const Node * node = device->node;
                            if ( node ) {
                                apsInst->updateNode ( *node );
                            }
                        }
                    }
#ifdef DEBUGV3
                    else {
                        const char * hex = ConvertToHexSpaceString ( data, (unsigned) size );                
                        logfile1 << "\t" << hex << endl;  
                    }
#endif
                }
            }
        }
    }
    DEBUGL ( else { name = addr ? addrs : "0x0000"; } )

#ifdef ENABLE_REACHABLE_TIMER
    DEBUGV1 ( ms << " " << name << " Indication src " );
#else
    DEBUGV1 ( name << " Indication eps:" << (int) ind.srcEndpoint () << " epd:" << (int) ind.dstEndpoint ()  );
#endif
}


////////////////////////////////////////////////////////////////////////////////
// Reachable check APS API: DataConfirm
////////////////////////////////////////////////////////////////////////////////

void PushBridge::apsdeDataConfirm ( const deCONZ::ApsDataConfirm & conf )
{
    DEBUGFTRACE ( "apsdeDataConfirm" );

    DEBUGL ( const char * addrs = "0x0000" );

    uint64_t addr = conf.dstAddress ().ext ();    
    
#ifdef ENABLE_REACHABLE_TIMER
    DEBUGL ( int ms = -1 );
#endif
    DEBUGL ( const char * name = "Unknown" );
    DEBUGL ( PushDevice * device = 0 );

    if ( addr ) {
        DEBUGL ( device = )
        GetPushDevice ( addr );

        DEBUGL ( addrs = qPrintable ( conf.dstAddress ().toStringExt () ) );
    }

#ifdef DEBUG
    if ( device ) {
        name    = device->name.c_str ();
#ifdef ENABLE_REACHABLE_TIMER
        ms      = device->lastMS;
    #endif
    }
    DEBUGL ( else { name = addrs; } )
#endif            
    
#ifdef ENABLE_REACHABLE_TIMER
    DEBUGV1 ( ms << " " << name << " Confirm dst " << " id:" << (int) conf.id () << " status:" << (int) conf.status () << " eps:" << (int) conf.srcEndpoint () << " epd:" << (int) conf.dstEndpoint () << endl );
#else
    DEBUGV1 ( name << " Confirm dst " << " id:" << (int) conf.id () << " status:" << (int) conf.status () << " eps:" << (int) conf.srcEndpoint () << " epd:" << (int) conf.dstEndpoint () << endl );
#endif
}


////////////////////////////////////////////////////////////////////////////////
// Reachable check APS API: nodeEvent
////////////////////////////////////////////////////////////////////////////////

void PushBridge::nodeEvent ( const deCONZ::NodeEvent & event )
{
    if ( !fhemActive )
        return;

    DEBUGFTRACE ( "nodeEvent" );

    const Node * node = event.node ();
    if ( !node ) {
        DEBUGV1 ( "Event: Invalid Node " << endl );
        return;
    }

    NodeEvent::Event ev = event.event ();

    bool updEvent = ( ev == NodeEvent::UpdatedSimpleDescriptor || ev == NodeEvent::NodeAdded );

    uint64_t addr = node->address ().ext ();

#ifdef ENABLE_REACHABLE_TIMER
    DEBUGL ( int ms = -1; )
#endif
    DEBUGL ( const char * name = "Unknown"; )
    
    PushDevice * device = 0;

    if ( !raspBee.mac && node->isCoordinator () ) {
        raspBee.mac     = addr;
        raspBee.node    = node;
        DEBUGL ( device = &raspBee; )
    }
    else {
        device = GetPushDevice ( addr, updEvent );        
    }

    if ( device ) {
        DEBUGL ( name    = device->name.c_str (); )
        
#ifdef ENABLE_REACHABLE_TIMER
        ms      = device->lastMS;
#endif
    }
    DEBUGL ( else { name    = ( addr ?  ( qPrintable ( node->address ().toStringExt () ) ) : "0x0000" ); } )

    DEBUGL ( unsigned int evi = ( unsigned int ) ev; )
    
    DEBUGL ( const char * ename = "Invalid"; )
    DEBUGL ( if ( evi < MAX_EVENT_NAME ) ename = eventNames [ evi ]; )

#ifdef ENABLE_REACHABLE_TIMER
    DEBUGV1 ( ms << " " << name << " Event:" << ename << endl );
#else
    DEBUGV1 ( name << " Event:" << ename << endl );
#endif
    if ( updEvent ) {
        UpdatePushDevice ( addr, node );

        if ( fhem_node_update ) {
            DEBUGV20 ( "\tnodeEvent: Updating node " << addr << " name:" << name );
            apsInst->updateNode ( *node );
            DEBUGV20 ( "\tnodeEvent: Updating node " << addr << " done." );
        }
    }
    else if ( ev == NodeEvent::NodeRemoved ) {
        if ( device ) {
            device->node = 0;
        }
    }
}


#ifdef ENABLE_REACHABLE_TIMER
////////////////////////////////////////////////////////////////////////////////
// Reachable check timer
////////////////////////////////////////////////////////////////////////////////

void PushBridge::TimerAlive ()
{
    DEBUGFTRACE ( "TimerAlive" );

    std::map < uint64_t, PushDevice * >::iterator it = devicesTimer.begin ();
    
	uint64_t ts = timerCurrent = time ( 0 );
    srand ( ( unsigned int ) ts );

    while ( it != devicesTimer.end () )
    {
        PushDevice * device = it->second;

		uint64_t diff = ts - device->lastTS;

        if ( device->type > PUSH_TYPE_SWITCH ) 
        {
            uint32_t typeDiff = 300;

            if ( device->type == PUSH_TYPE_LIGHT ) {
                typeDiff = 140;
            }

            if ( diff >= typeDiff )
            {
                //device->lastMS = -1;
                if ( __sync_val_compare_and_swap ( & device->available, true, false ) == true ) 
                {
                    if ( device->name.length () > 0 )
                    {                        
                        DEBUGV2 ( "TimerAlive: Reachable 0 to mac " << device->mac );

                        char cmd [ 128 ];
                        const char * name = device->name.c_str ();

                        DEBUGV ( "TimerAlive: Reachable 0 to device:" << name << " ms:" << diff );
                        DEBUGV1 ( "TimerAlive: Reachable 0 to device:" << name << " ms:" << diff );

                        snprintf ( cmd, 128, "setreading %s reachable 0\nsetreading %s _reachable 0\n", name, name );

                        EnqueueToFhem ( cmd );

                        if ( fhem_node_update ) {
                            const Node * node = device->node;
                            if ( node ) {
                                DEBUGV2 ( "TimerAlive: Updating node. mac:" << device->mac << " name:" << name );

                                apsInst->updateNode ( *device->node );
                            }
                        }
                    }
                }
            }
        }
        
        device->lastMS = ( int ) diff;
        ++it;
    }
}
#endif


void RestNodeBase::UpdateToFhem ( PushDevice * device )
{
    DEBUGFTRACE ( "RestNodeBase::UpdateToFhem" );

    if ( !device->needsRestUpdate ) {
        return;
    }

    const char * name = device->name.c_str ();

	if ( name && strlen ( name ) > 0 ) 
    {
	    char cmd [ 256 ];

		snprintf ( cmd, 256, "setreading %s id %s\n"
			"setreading %s uid %s\n"
			"setreading %s available %i\n"
#ifdef DEBUG
			"setreading %s mgmtBindSupported %i\n"
			"setreading %s read %i\n"
			"setreading %s lastAttributeReportBind %i\n"
#endif
			,
			name, qPrintable ( m_id ),
			name, qPrintable ( m_uid ),
			name, ( int ) m_available
#ifdef DEBUG
			,
			name, ( int ) m_mgmtBindSupported,
			name, ( int ) m_read,
			name, ( int ) m_lastAttributeReportBind
#endif
		);

		pushBridge.EnqueueToFhem ( cmd );
	}
}


void LightNode::UpdateToFhem ( PushDevice * device )
{
    DEBUGFTRACE ( "LightNode::UpdateToFhem" );

    if ( !device->needsRestUpdate ) {
        return;
    }

    if ( m_type.contains ( "plug" ) ) {
        device->type = m_pushType = PUSH_TYPE_PLUG;
    }

    const char * name = device->name.c_str ();

	if ( name && strlen ( name ) > 0 ) 
    {
    	char cmd [ 1024 ];

        if ( !m_manufacturer.length () || !m_modelId.length () || !m_swBuildId.length () || !m_type.length () ) {
            return;
        }

		snprintf ( cmd, 1024, "setreading %s manufacturer %s\n"
			"setreading %s modelId %s\n"
			"setreading %s swBuildId %s\n"
			"setreading %s type %s\n"
			"setreading %s manufacturerCode %i\n"
			"setreading %s clusterId %i\n"
#ifdef DEBUG
			"setreading %s resetRetryCount %i\n"
			"setreading %s zdpResetSeq %i\n"
#endif
			"setreading %s groupCapacity %i\n"
			"setreading %s on %i\n"
			"setreading %s level %i\n"
			"setreading %s groupCount %i\n"
			"setreading %s sceneCapacity %i\n",
			name, qPrintable ( m_manufacturer ),
			name, qPrintable ( m_modelId ),
			name, qPrintable ( m_swBuildId ),
			name, qPrintable ( m_type ),
			name, ( int ) m_manufacturerCode,
			name, ( int ) m_otauClusterId,
#ifdef DEBUG
			name, ( int ) m_resetRetryCount,
			name, ( int ) m_zdpResetSeq,
#endif
			name, ( int ) m_groupCapacity,
			name, ( int ) m_isOn,
			name, ( int ) m_level,
			name, ( int ) m_groupCount,
			name, ( int ) m_sceneCapacity
		);

		pushBridge.EnqueueToFhem ( cmd );
		
		RestNodeBase::UpdateToFhem ( device );

        if ( device->type != PUSH_TYPE_PLUG )
        {
            snprintf ( cmd, 1024, "setreading %s hasColor %i\n"
                "setreading %s hue %i\n"
                "setreading %s ehue %i\n"
                "setreading %s normHue %f\n"
                "setreading %s sat %i\n"
                "setreading %s colorX %i\n"
                "setreading %s colorY %i\n"
                "setreading %s colorTemperature %i\n"
                "setreading %s colorMode %s\n"
                "setreading %s colorLoopActive %i\n"
                "setreading %s colorLoopSpeed %i\n",
                name, m_hasColor ? 1 : 0,
                name, ( int ) m_hue,
                name, ( int ) m_ehue,
                name, m_normHue,
                name, ( int ) m_sat,
                name, ( int ) m_colorX,
                name, ( int ) m_colorY,
                name, ( int ) m_colorTemperature,
                name, m_colorMode.length () ? qPrintable ( m_colorMode ) : "undef",
                name, m_colorLoopActive ? 1 : 0,
                name, m_colorLoopSpeed
            );

            pushBridge.EnqueueToFhem ( cmd );
        }

        device->needsRestUpdate = false;
	}
}


void Sensor::UpdateToFhem ( PushDevice * device )
{
    DEBUGFTRACE ( "Sensor::UpdateToFhem" );

    if ( !device->needsRestUpdate ) {
        return;
    }

    const char * name = device->name.c_str ();

	if ( name && strlen ( name ) > 0 ) 
    {
	    char cmd [ 512 ];

        if ( !m_name.length () || !m_type.length () || !m_modelid.length () || !m_manufacturer.length ()
             || !m_swversion.length () ) {
            return;
        }
		snprintf ( cmd, 512, "setreading %s deletedstate %i\n"
			"setreading %s name %s\n"
			"setreading %s type %s\n"
			"setreading %s modelid %s\n"
			"setreading %s manufacturer %s\n"
			"setreading %s swversion %s\n"
			"setreading %s mode %i\n"
#ifdef DEBUG
			"setreading %s resetRetryCount %i\n"
			"setreading %s zdpResetSeq %i\n"
#endif
			"setreading %s etag %s\n",
			name, ( int ) m_deletedstate,
			name, qPrintable ( m_name ),
			name, qPrintable ( m_type ),
			name, qPrintable ( m_modelid ),
			name, qPrintable ( m_manufacturer ),
			name, qPrintable ( m_swversion ),
			name, m_mode,
#ifdef DEBUG
			name, m_resetRetryCount,
			name, m_zdpResetSeq,
#endif
			name, etag.length () ? qPrintable ( etag ) : "undef"
		);

		pushBridge.EnqueueToFhem ( cmd );

        m_state.m_pushDevice = device;
        m_state.UpdateToFhem ( device );

        m_config.m_pushDevice = device;
        m_config.UpdateToFhem ( device );  

		RestNodeBase::UpdateToFhem ( device );

        device->needsRestUpdate = false;
	}
}


void Sensor::UpdateSensorFhem ()
{
    DEBUGFTRACE ( "Sensor::UpdateSensorFhem" );
    
	if ( !m_pushDevice ) {
    	uint64_t addr 	= address ().ext ();
		if ( addr ) {
			PushDevice * device = pushBridge.GetPushDevice ( addr );
			if ( device ) {
				device->node = node ();
				device->type = m_pushType;    		
				m_pushDevice = device;
				
				if ( device->needsRestUpdate ) {
        			UpdateToFhem ( device );
				}
			}
		}
	}
}


void SensorState::UpdateToFhem ( PushDevice * device )
{
    DEBUGFTRACE ( "SensorState::UpdateToFhem" );

    const char * name = device->name.c_str ();
    
	if ( name && strlen ( name ) > 0 ) 
    {
	    char cmd [ 512 ];

		snprintf ( cmd, 512, "setreading %s lastupdated %s\n"
			"setreading %s flag %s\n"
			"setreading %s status %s\n"
			"setreading %s open %s\n"
			"setreading %s lux %i\n",
			name, m_lastupdated.length () ? qPrintable ( m_lastupdated ) : "undef",
			name, m_flag.length () ? qPrintable ( m_flag ) : "undef",
			name, m_status.length () ? qPrintable ( m_status ) : "undef",
			name, m_open.length () ? qPrintable ( m_open ) : "undef",
			name, m_lux
		);
		pushBridge.EnqueueToFhem ( cmd );

        if ( m_pushDevice && m_pushDevice->type != PUSH_TYPE_SWITCH )
        {
            snprintf ( cmd, 512, "setreading %s presence %s\n"
                "setreading %s temperature %s\n"
                "setreading %s humidity %s\n"
                "setreading %s daylight %s\n",
                name, m_presence.length () ? qPrintable ( m_presence ) : "undef",
                name, m_temperature.length () ? qPrintable ( m_temperature ) : "undef",
                name, m_humidity.length () ? qPrintable ( m_humidity ) : "undef",
                name, m_daylight.length () ? qPrintable ( m_daylight ) : "undef"
            );
            pushBridge.EnqueueToFhem ( cmd );
        }
	}
}


void SensorConfig::UpdateToFhem ( PushDevice * device )
{
    DEBUGFTRACE ( "SensorConfig::UpdateToFhem" );

    const char * name = device->name.c_str ();

	if ( name && strlen ( name ) > 0 ) 
    {
	    char cmd [ 512 ];

        DEBUGFTRACE ( "SensorConfig::UpdateToFhem 1" );

		snprintf ( cmd, 512, "setreading %s on %i\n"
			"setreading %s reachable %i\n"
			"setreading %s duration %f\n"
			"setreading %s battery %i\n"
			"setreading %s url %s\n",
			name, m_on ? 1 : 0,
			name, m_reachable ? 1 : 0,
			name, m_duration,
			name, m_battery,
			name, m_url.length () ? qPrintable ( m_url ) : "undef"
		);

		pushBridge.EnqueueToFhem ( cmd );
		
		if ( m_pushDevice && m_pushDevice->type != PUSH_TYPE_SWITCH )
		{
			snprintf ( cmd, 512, "setreading %s longitude %s\n"
				"setreading %s latitude %s\n"
				"setreading %s sunriseoffset %s\n"
				"setreading %s sunsetoffset %s\n",
				name, m_long.length () ? qPrintable ( m_long ) : "undef",
				name, m_lat.length () ? qPrintable ( m_lat ) : "undef",
				name, m_sunriseoffset.length () ? qPrintable ( m_sunriseoffset ) : "undef",
				name, m_sunsetoffset.length () ? qPrintable ( m_sunsetoffset ) : "undef"
			);
		}
	}
}

#endif