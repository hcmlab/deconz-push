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
#include "ct_push_bridge.h"
#include "de_web_plugin_private.h"
#include "colorspace.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <exception>
#include <time.h>
#include <sys/stat.h>

#define USE_SSL

#ifdef USE_SSL
    #include <openssl/ssl.h>
    #include <openssl/err.h>
#	if (SSLEAY_VERSION_NUMBER >= 0x0907000L)
#		include <openssl/conf.h>
#	endif
#endif

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

extern DeRestPluginPrivate * exportedRestPlugin;

const char * UNKNOWN_DEVICE_NAME = "Unknown";

const char *    configFile      = "/usr/share/deCONZ/rest_push.conf";
const char *    bridgeConfFile  = "/usr/share/deCONZ/rest_bridge.conf";

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


void PushBridge::RequestConfig ()
{
    DEBUGFTRACE ( "RequestConfig" );
    
    char cmd [ 128 ];
    snprintf ( cmd, 128, "{deCONZ_get_config('%llu')}\n", raspBee.mac );
    
    EnqueueToFhem ( cmd );
}


void PushBridge::SaveCache ()
{
    DEBUGFTRACE ( "SaveCache" );

    ofstream bridgeFile ( bridgeConfFile );

    if ( bridgeFile.good () )
    {
        DEBUGFTRACE ( "SaveCache: Saving bridge id " << fbridge_id );

        bridgeFile << fbridge_id << endl;

        DEBUGFTRACE ( "SaveCache: Saving raspBee.mac " << raspBee.mac );

        bridgeFile << raspBee.mac << endl;

        bridgeFile.close ();
    }
}


void PushBridge::LoadConfigFile ()
{
    DEBUGFTRACE ( "LoadConfigFile" );

    bool restartFhemThread  = false;
    bool restartPushThread  = false;
    bool _enable_fhem_tunnel = true;
    bool _enable_push       = true;
    bool _enable_ssl       = false;

    ifstream bridgeFile ( bridgeConfFile );

    if ( bridgeFile.good () )
    {
        DEBUGFTRACE ( "LoadConfigFile: Loading bridge conf ..." );

        string line;
        if ( getline ( bridgeFile, line ) )
        {
            int value;                    
            std::istringstream iss ( line );

            if ( iss >> value ) {
                if ( value > 0 ) {
                    fbridge_id = value;
                    DEBUGFTRACE ( "LoadConfigFile: Using bridge ID number " << value );
                }
            }

            if ( getline ( bridgeFile, line ) )
            {                        
                std::istringstream iss ( line );

                if ( iss >> value ) {
                    if ( value > 0 && raspBee.mac <= 0 ) {
                        raspBee.mac = value;
                        DEBUGFTRACE ( "LoadConfigFile: Using raspBee.mac " << value );
                    }
                }
            }
        }
        bridgeFile.close ();
    }

    ifstream mapFile ( configFile );

    if ( mapFile.good () )
    {
        DEBUGFTRACE ( "LoadConfigFile: Loading configuration ..." );

        string line;
        while ( getline ( mapFile, line ) )
        {            
            const char * chars = line.c_str ();
            if ( line.size () < 3 || chars [ 0 ] == '#' )
                continue;

            // Disable push extension
            if ( !line.compare ( "disableplugin" ) ) {
                enable_plugin = false;
                DEBUGFTRACE ( "LoadConfigFile: Disabled push extension." );
                break;
            }
            else if ( !line.compare ( "nonodeupdate" ) ) {
                fhem_node_update = false;
                DEBUGFTRACE ( "LoadConfigFile: Disabled fhem node update." );
            }
            else if ( !line.compare ( "disablefhem" ) ) {
                _enable_fhem_tunnel = false;
                DEBUGFTRACE ( "LoadConfigFile: Disabled fhem tunnel." );
            }
            else if ( !line.compare ( "disablepushlistener" ) ) {
                _enable_push = false;
                DEBUGFTRACE ( "LoadConfigFile: Disabled push socket." );
            }
            else if ( !line.compare ( "ssl" ) ) {
                _enable_ssl = true;
                DEBUGFTRACE ( "LoadConfigFile: Enabling ssl." );
            }
            else {
                const char * ptr = line.c_str ();
                if ( ptr [ 0 ] == 'f' && ptr [ 1 ] == 'i' && ptr [ 2 ] == 'p' && ptr [ 3 ] == ' ' ) {
                    string optname; string value;
                    
                    std::istringstream iss ( line );

                    if ( iss >> optname >> value ) {
                        struct sockaddr_in addr;

                        if ( inet_aton ( value.c_str (), &addr.sin_addr ) ) {
                            if ( fhem_ip_addr.compare ( value ) ) {
                                fhem_ip_addr = value;
                                restartFhemThread = true;
                                DEBUGFTRACE ( "LoadConfigFile: Using fhem ip " << value );
                            }
                        }
                    }
                }
                else if ( ptr [ 0 ] == 'f' && ptr [ 1 ] == 'p' && ptr [ 2 ] == 'a' && ptr [ 3 ] == 's' && ptr [ 4 ] == 's' && ptr [ 5 ] == ' ' ) {
                    string optname; string value;
                    
                    std::istringstream iss ( line );

                    if ( iss >> optname >> value ) {
                        if ( fhemPassword.compare ( value ) ) {
                            fhemPassword = value;
                            restartFhemThread = true;
                            //DEBUGFTRACE ( "LoadConfigFile: Using fhem password " << value );
                            DEBUGFTRACE ( "LoadConfigFile: Using fhem password " );
                        }
                    }
                }
                else {
                    string optname; int value;
                    
                    std::istringstream iss ( line );

                    if ( iss >> optname >> value ) {
                        if ( !optname.compare ( "fport" ) ) {
                            if ( value > 0 && value < 66000 ) {
                                if ( fhem_port != value ) {
                                    fhem_port = value; restartFhemThread = true;
                                    DEBUGFTRACE ( "LoadConfigFile: Using fhem port " << value );
                                }
                            }
                        }
                        else if ( !optname.compare ( "pport" ) ) {
                            if ( value > 0 && value < 66000 ) {
                                if ( bridge_push_port != value ) {
                                    bridge_push_port = value; restartPushThread = true;
                                    DEBUGFTRACE ( "LoadConfigFile: Using push port " << value );
                                }
                            }
                        }
                        else {
                            DEBUGFTRACE ( "LoadConfigFile: Unknown config line " << line );
                        }
                    }
                }
            }
        }

        mapFile.close ();
    }

    if ( !enable_plugin ) {
        DEBUGFTRACE ( "LoadConfigFile: Disabling plugin. " );
        enable_fhem_tunnel = enable_push = false;
    }
    else {
        if ( fhemSSL != _enable_ssl ) {
            fhemSSL = _enable_ssl;
            restartFhemThread = true;
            DEBUGFTRACE ( "LoadConfigFile: Restarting fhem thread due to ssl option." );
        }

        if ( _enable_fhem_tunnel != enable_fhem_tunnel ) {
            enable_fhem_tunnel = _enable_fhem_tunnel; restartFhemThread = true;
            DEBUGFTRACE ( "LoadConfigFile: Restarting fhem thread. " );
        }

        if ( _enable_push != enable_push ) {
            enable_push = _enable_push; restartPushThread = true;
            DEBUGFTRACE ( "LoadConfigFile: Restarting push thread. " );
        }
    }

    if ( restartFhemThread ) HandleFhemThread ();
    if ( restartPushThread ) HandlePushThread ();
}


////////////////////////////////////////////////////////////////////////////////
// PushBridge Constructor
////////////////////////////////////////////////////////////////////////////////

PushBridge::PushBridge () : 
    fhemSocket ( -1 ), fhemThreadRun ( false ), fhemListenerRun ( false ), acceptSocket ( -1 ), acceptThreadRun ( false ), 
    pushSocket ( -1 ), pushThreadRun ( false ), apsInst ( 0 ),
    enable_plugin ( true ), enable_fhem_tunnel ( false ), enable_push ( false ),
    fhemActive ( false ), fhemListenerActive ( false ), acceptActive ( false ), pushActive ( false ), 
    fhemSSL ( false ), fhemSSLInitialized ( false ), conectTime ( 0 ), fhemAuthOK ( true ), ssl_ctx ( 0 ), ssl_web ( 0 ), ssl_ptr ( 0 ),
    fhem_node_update ( false ), fhem_ip_addr ( "127.0.0.1" ), fhem_port ( 7072 ), bridge_push_port ( 7073 ), fbridge_id ( -1 )
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

    fhemPassword = "";

    raspBee.available   = true;

    // Init threads
    memset ( &fhemThread, 0, sizeof (fhemThread) );
    memset ( &fhemListener, 0, sizeof (fhemListener) );
    
    memset ( &acceptThread, 0, sizeof (acceptThread) );
    memset ( &pushThread, 0, sizeof (pushThread) );

    DEBUGV1 ( "PushBridge::Construct: Initializing thread resources." );

    pthread_mutex_init ( &fhemQueueLock,	0 );
    pthread_mutex_init ( &devicesLock,		0 );
    pthread_mutex_init ( &groupLock,		0 );

    pthread_cond_init ( &fhemSignal,        NULL );

    pthread_mutex_init ( &pushLock,	        0 );
    pthread_cond_init ( &pushSignal,        NULL );    

    DEBUGV1 ( "PushBridge::Construct: Loading configuration." );

    LoadConfigFile ();

    if ( !enable_plugin ) { return; }

    //connect ( apsInst, SIGNAL ( apsdeDataConfirm ( const deCONZ::ApsDataConfirm & ) ),
    //        this, SLOT ( apsdeDataConfirm ( const deCONZ::ApsDataConfirm & ) ) );

    connect ( apsInst, SIGNAL ( apsdeDataIndication ( const deCONZ::ApsDataIndication & ) ),
            this, SLOT ( apsdeDataIndication ( const deCONZ::ApsDataIndication & ) ) );

    connect ( apsInst, SIGNAL ( nodeEvent ( deCONZ::NodeEvent ) ),
            this, SLOT ( nodeEvent ( deCONZ::NodeEvent ) ) );
}


void PushBridge::HandleFhemThread ()
{
    DEBUGFTRACE ( "HandleFhemThread" );

    if ( fhemThreadRun ) {
        fhemThreadRun = false;
        fhemListenerRun = false;

        if ( fhemSocket != -1 ) {
            ::shutdown ( fhemSocket, 2 );
            ::close ( fhemSocket );
            fhemSocket = -1;
        }

        SignalFhemThread ();

        DEBUGFTRACE ( "HandleFhemThread: Waiting for them thread." );
        // Wait for thread exit
        pthread_join ( fhemThread, 0 );

        // Wait for listener thread exit
        //pthread_join ( fhemListener, 0 ); // Dont do this.. as the listener may call this - deadlock

        memset ( &fhemThread, 0, sizeof (fhemThread) );
        memset ( &fhemListener, 0, sizeof (fhemListener) );
    }
    
    EmptyFhemQueue ();

    if ( enable_fhem_tunnel ) {
        fhemThreadRun = true;

        DEBUGFTRACE ( "HandleFhemThread: Starting thread." );

        pthread_create ( &fhemThread, 0, FhemThreadStarter, this );
    }

    DEBUGFTRACE ( "HandleFhemThread: done" );
}


void PushBridge::HandlePushThread ()
{
    DEBUGFTRACE ( "HandlePushThread" );

    if ( pushThreadRun || acceptThreadRun ) {
        pushThreadRun = false;
        acceptThreadRun = false;

        if ( acceptSocket != -1 ) {
            ::shutdown ( acceptSocket, 2 );
            ::close ( acceptSocket );
            acceptSocket = -1;
        }

        if ( pushSocket != -1 ) {
            ::shutdown ( pushSocket, 2 );
            ::close ( pushSocket );
            pushSocket = -1;
        }

        SignalPushThread ();

        // Wait for thread exit
        //pthread_join ( pushThread, 0 );

        // Wait for listener thread exit
        DEBUGFTRACE ( "HandlePushThread: Waiting for acceptThread." );
        pthread_join ( acceptThread, 0 );
    
        memset ( &acceptThread, 0, sizeof (acceptThread) );
        memset ( &pushThread, 0, sizeof (pushThread) );
    }

    if ( enable_push ) {
        acceptThreadRun = true;

        DEBUGFTRACE ( "HandlePushThread: Starting accept thread." );

        pthread_create ( &acceptThread, 0, AcceptThreadStarter, this );

        pushThreadRun = true;

        //DEBUGV1 ( "PushBridge::Construct: Starting push thread." );

        //pthread_create ( &pushThread, 0, PushThreadStarter, this );
    }

    DEBUGFTRACE ( "HandlePushThread: done" );
}

////////////////////////////////////////////////////////////////////////////////
// PushBridge deconstructor
////////////////////////////////////////////////////////////////////////////////

#ifdef USE_SSL

void DisposeSSL ( void * &ssl_ctx, void * &ssl_web )
{
    SSL_CTX     *   ctx = ( SSL_CTX * ) ssl_ctx;
    BIO         *   web = ( BIO * ) ssl_web;

	if ( web != NULL ) { BIO_free_all ( web ); ssl_web = 0; }

	if ( NULL != ctx ) { SSL_CTX_free ( ctx ); ssl_ctx = 0; }
}

#endif

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
    
    std::map < int, PushGroup * >::iterator itg = groups.begin ();
    
    while ( itg != groups.end () ) {
        PushGroup * group = itg->second;
        delete group;
        ++itg;
    }

    groups.clear ();

    fhemThreadRun = false;
    fhemListenerRun = false;

    if ( fhemSocket != -1 ) {
        ::shutdown ( fhemSocket, 2 );
        ::close ( fhemSocket );
        fhemSocket = -1;
    }

#ifdef USE_SSL
    DisposeSSL ( ssl_ctx, ssl_web );
#endif

    SignalFhemThread ();

    pushThreadRun = false;
    acceptThreadRun = false;

    if ( acceptSocket != -1 ) {
        ::shutdown ( acceptSocket, 2 );
        ::close ( acceptSocket );
        acceptSocket = -1;
    }

    if ( pushSocket != -1 ) {
        ::shutdown ( pushSocket, 2 );
        ::close ( pushSocket );
        pushSocket = -1;
    }

    SignalPushThread ();

    // Wait for thread exit
    pthread_join ( fhemThread, 0 );

    // Wait for listener thread exit
    pthread_join ( fhemListener, 0 );

    // Wait for thread exit
    //pthread_join ( pushThread, 0 );

    pthread_join ( acceptThread, 0 );
    
    EmptyFhemQueue ();

    EmptyPushQueue ();

    pthread_cond_destroy ( &fhemSignal );

    pthread_mutex_destroy ( &fhemQueueLock );
    pthread_mutex_destroy ( &groupLock );
	pthread_mutex_destroy ( &devicesLock );

    pthread_cond_destroy ( &pushSignal );

    pthread_mutex_destroy ( &pushLock );
}


////////////////////////////////////////////////////////////////////////////////
// FHEM channel injection
////////////////////////////////////////////////////////////////////////////////

void PushBridge::SetNode ( Node * node )
{
    DEBUGFTRACE ( "SetNode" );

    if ( !node || !fhemActive )
        return;
	
    uint64_t addr 	= node->address ().ext ();

    PushDevice * device = pushBridge.GetPushDevice ( addr );
    if ( device ) {
        device->node = node;
    }
}


void PushBridge::SetNodeAvailable ( char type, int id, bool available )
{
    DEBUGFTRACE ( "SetNodeAvailable" );

    if ( !fhemActive && !pushActive ) { return; }

    if ( id <= 0 ) {
        DEBUGV ( "SetNodeAvailable: Error Invalid id for " << type << id << " fbridge_id [ " << fbridge_id << " ]" );
        return;
    }

    int reachable = ( int ) available;

    DEBUGV2 ( "SetNodeAvailable: Reachable " << reachable << " to device " << type << id );

    char buffer [ 256 ];

    DEBUGV ( "SetNodeAvailable: Reachable " << reachable << " to device: " << type << id );
    DEBUGV1 ( "SetNodeAvailable: Reachable " << reachable << " to device: " << type << id );

    int len = snprintf ( buffer, 256, "{pushupd1('%i^%c^%i^reachable^%i^available^%i')}\n",
        fbridge_id, type, id, reachable, reachable );
    
    if ( fhemActive && fbridge_id > 0 )
        EnqueueToFhem ( buffer );

    if ( pushActive ) {
        EnqueueToPush ( buffer, len );
    }
}


void PushBridge::SetNodeId ( char type, int id )
{
    DEBUGFTRACE ( "SetNodeId 1" );

    if ( !fhemActive && !pushActive ) { return; }

    DEBUGFTRACE ( "SetNodeId 11" );

    if ( id <= 0 ) {
        DEBUGV ( "SetNodeId: Error Invalid id for " << type << id << " fbridge_id [ " << fbridge_id << " ]" );
        return;
    }

    DEBUGV2 ( "SetNodeId: " << id << " to device " << type << id );

    char buffer [ 256 ];
	int len = snprintf ( buffer, 256, "{pushupd1('%i^%c^%i^id^%i')}\n", fbridge_id, type, id, id );

    DEBUGFTRACE ( "SetNodeId 15" );
    if ( fhemActive && fbridge_id > 0 )
        EnqueueToFhem ( buffer );

    if ( pushActive ) {
        EnqueueToPush ( buffer, len );
    }
}


void PushBridge::SetNodeUid ( char type, int id, const QString & m_uid )
{
    DEBUGFTRACE ( "SetNodeUid 1" );

    if ( !fhemActive && !pushActive ) { return; }
    
    DEBUGFTRACE ( "SetNodeUid 11" );

    if ( id <= 0 ) {
        DEBUGV ( "SetNodeUid: Error Invalid id for " << type << id << " fbridge_id [ " << fbridge_id << " ]" );
        return;
    }

    DEBUGV2 ( "SetNodeUid: " << qPrintable ( m_uid ) << " to device " << type << id );

    char buffer [ 256 ];
	int len = snprintf ( buffer, 256, "{pushupd1('%i^%c^%i^uniqueid^%s')}\n", fbridge_id, type, id, qPrintable ( m_uid ) );

    DEBUGFTRACE ( "SetNodeUid 15" );
    if ( fhemActive && fbridge_id > 0 )
        EnqueueToFhem ( buffer );

    if ( pushActive ) {
        EnqueueToPush ( buffer, len );
    }
}


void PushBridge::SetNodeInfo ( char type, int id, const char * reading, const QString & value )
{
    DEBUGFTRACE ( "SetNodeInfo 1:" << reading );

    if ( !fhemActive && !pushActive ) { return; }

    DEBUGFTRACE ( "SetNodeInfo 11" );

    if ( id <= 0 ) {
        DEBUGV ( "SetNodeInfo: Error Invalid id for " << type << id << " fbridge_id [ " << fbridge_id << " ]" );
        return;
    }

    DEBUGV2 ( "SetNodeInfo: " << reading << " " << qPrintable ( value ) << " to device " << type << id );

    char buffer [ 256 ];
	int len = snprintf ( buffer, 256, "{pushupd1('%i^%c^%i^%s^%s')}\n", fbridge_id, type, id, reading, qPrintable ( value ) );

    DEBUGFTRACE ( "SetNodeInfo 15" );
    if ( fhemActive && fbridge_id > 0 )
        EnqueueToFhem ( buffer );

    if ( pushActive ) {
        EnqueueToPush ( buffer, len );
    }
}


void PushBridge::SetNodeInfo ( char type, int id, const char * reading, QString & value ) {
    SetNodeInfo ( type, id, reading, (const QString &) value );
}


void PushBridge::SetNodeInfo ( char type, int id, const char * reading, uint32_t value )
{
    DEBUGFTRACE ( "SetNodeInfo 3: " << reading << " v:" << value );

    if ( !fhemActive && !pushActive ) { return; }

    if ( id <= 0 ) {
        DEBUGV ( "SetNodeInfo: Error Invalid id for " << type << id << " fbridge_id [ " << fbridge_id << " ]" );
        return;
    }
    
    DEBUGV2 ( "SetNodeInfo: " << reading << " " << value << " to device " << type << id );

    char buffer [ 256 ];
	int len = snprintf ( buffer, 256, "{pushupd1('%i^%c^%i^%s^%i')}\n", fbridge_id, type, id, reading, value );
    
    if ( fhemActive && fbridge_id > 0 )
        EnqueueToFhem ( buffer );

    if ( pushActive ) {
        EnqueueToPush ( buffer, len );
    }
}


void PushBridge::SetNodeInfoDouble ( char type, int id, const char * reading, double value )
{
    DEBUGFTRACE ( "SetNodeInfoDouble" );

    if ( !fhemActive && !pushActive ) { return; }

    if ( id <= 0 ) {
        DEBUGV ( "SetNodeInfoDouble: Error Invalid id for " << type << id << " fbridge_id [ " << fbridge_id << " ]" );
        return;
    }
    
    DEBUGV2 ( "SetNodeInfoDouble: " << reading << " " << value << " to device " << type << id );

    char buffer [ 256 ];
	int len = snprintf ( buffer, 256, "{pushupd1('%i^%c^%i^%s^%f')}\n", fbridge_id, type, id, reading, value );
    
    if ( fhemActive && fbridge_id > 0 )
        EnqueueToFhem ( buffer );

    if ( pushActive ) {
        EnqueueToPush ( buffer, len );
    }
}


void PushBridge::SetNodeState ( RestNodeBase * rnode, char type, int id, bool reading, int level )
{
    DEBUGFTRACE ( "SetNodeState" );

    if ( !fhemActive && !pushActive ) { return; }

    if ( id <= 0 ) {
        DEBUGV ( "SetNodeState: Error Invalid id for " << type << id << " fbridge_id [ " << fbridge_id << " ]" );
        return;
    }
    
    if ( rnode->needsRestUpdate ) rnode->UpdateToFhem ();
    
    DEBUGV2 ( "SetNodeState: " << reading << " " << level << " to device " << type << id );

    char buffer [ 256 ];
	int len = snprintf ( buffer, 256, "{pushupd1('%i^%c^%i^state^%s^onoff^%d')}\n", fbridge_id, type, id, reading ? "on" : "off", reading ? 1 : 0 );
	
    if ( fhemActive && fbridge_id > 0 )
        EnqueueToFhem ( buffer );

    if ( pushActive ) {
        EnqueueToPush ( buffer, len );
    }
	
	if ( level >= 0 ) {		
		int slevel = reading ? level : 0;
		int spct = reading ? ( ( level * 100 ) / 253 ) : 0;
		
		int len = snprintf ( buffer, 256, "{pushupd1('%i^%c^%i^level^%d^bri^%d^pct^%d')}\n", fbridge_id, type, id, slevel, slevel, spct );
		
        if ( fhemActive && fbridge_id > 0 )
		    EnqueueToFhem ( buffer );

        if ( pushActive ) {
            EnqueueToPush ( buffer, len );
        }
	}
}


void PushBridge::SetGroupInfo ( QString & qid, const char * reading, uint32_t value )
{
    if ( !reading ) return;
    if ( !fhemActive && !pushActive ) { return; }

    DEBUGV ( "SetGroupInfo 2: " << qPrintable ( qid ) << " " << reading << " [ " << value << " ]" );

    int id = qid.toInt ();
    if ( id <= 0 ) { return; }

    char buffer [ 128 ];
    int len = snprintf ( buffer, 128, "{pushupd1('%i^g^%i^%s^%i')}\n", fbridge_id, id, reading, value );

    if ( fhemActive && fbridge_id > 0 )
        EnqueueToFhem ( buffer );

    if ( pushActive ) {
        EnqueueToPush ( buffer, len );
    }
}


void PushBridge::SetGroupInfo ( QString & qid, const char * reading, float value )
{
    if ( !reading ) return;
    if ( !fhemActive && !pushActive ) { return; }

    DEBUGFTRACE ( "SetGroupInfo 3: " << qPrintable ( qid ) << " " << reading << " [ " << value << " ]" );

    int id = qid.toInt ();
    if ( id <= 0 ) { return; }

    char buffer [ 128 ];
    int len = snprintf ( buffer, 128, "{pushupd1('%i^g^%i^%s^%f')}\n", fbridge_id, id, reading, value );

    if ( fhemActive && fbridge_id > 0 )
        EnqueueToFhem ( buffer );

    if ( pushActive ) {
        EnqueueToPush ( buffer, len );
    }
}


void PushBridge::SetGroupInfo ( QString & qid, const char * reading, const char * value )
{
    if ( !reading || !value ) return;
    if ( !fhemActive && !pushActive ) { return; }

    DEBUGFTRACE ( "SetGroupInfo 4: " << qPrintable ( qid ) << " " << reading << " [ " << value << " ]" );

    int id = qid.toInt ();
    if ( id <= 0 ) { return; }

    char buffer [ 128 ];
    int len = snprintf ( buffer, 128, "{pushupd1('%i^g^%i^%s^%s')}\n", fbridge_id, id, reading, value );

    if ( fhemActive && fbridge_id > 0 )
        EnqueueToFhem ( buffer );

    if ( pushActive ) {
        EnqueueToPush ( buffer, len );
    }
}


PushGroup * PushBridge::GetPushGroup ( QString & qid )
{
    DEBUGFTRACE ( "GetPushGroup" );
	if ( qid.length () <= 0 )
		return 0;

	PushGroup	* group		= 0;
	int id	= qid.toInt ();

	if ( !pthread_mutex_lock ( &groupLock ) )
	{
		map < int, PushGroup * >::iterator it = groups.find ( id );
		if ( it != groups.end () )
		{
			group = it->second;
		}
		else {
			DEBUGV ( "GetPushGroup: PushGroup for " << id << " NOT found. Creating new ..." );
            group = new PushGroup ();
            
            if ( group ) {
                groups [ id ] = group;
            }
		}

		if ( pthread_mutex_unlock ( &groupLock ) ) {
			DEBUGV ( "GetPushGroup: Failed to unlock." );
		}
	}
    
    return group;
}


void PushGroup::Update ( QString & id, bool o, uint16_t x, uint16_t y, uint16_t h, float hf, uint16_t s, uint16_t l, uint16_t temp )
{
    if ( !pushBridge.fhemActive && !pushBridge.pushActive ) { return; }

    DEBUGFTRACE ( "PushGroup::Update" );

    if ( on != o ) {
        on = o;
        pushBridge.SetGroupInfo ( id, "reachable", ( uint32_t ) 1 );
    //    pushBridge.SetGroupInfo ( id, "on", ( uint32_t ) ( o ? 1 : 0 ) );
    }

    if ( colorX != x ) {
        colorX = x;
        pushBridge.SetGroupInfo ( id, "colorX", ( uint32_t ) x );
    }

    if ( colorY != y ) {
        colorY = y;
        pushBridge.SetGroupInfo ( id, "colorY", ( uint32_t ) y );
    }

    if ( hue != h ) {
        hue = h;
        pushBridge.SetGroupInfo ( id, "hue", ( uint32_t ) h );
    }

    if ( sat != s ) {
        sat = s;
        pushBridge.SetGroupInfo ( id, "sat", ( uint32_t ) s );
    }

    if ( level != l ) {
        level = l;
        pushBridge.SetGroupInfo ( id, "level", ( uint32_t ) l );
        pushBridge.SetGroupInfo ( id, "bri", ( uint32_t ) l );
        
		int pct = l ? ( ( l * 100 ) / 253 ) : 0;
        pushBridge.SetGroupInfo ( id, "pct", ( uint32_t ) pct );
    }

    if ( colorTemperature != temp ) {
        colorTemperature = temp;
        pushBridge.SetGroupInfo ( id, "colorTemperature", ( uint32_t ) temp );
    }

    if ( hueFloat != hf ) {
        hueFloat = hf;
        pushBridge.SetGroupInfo ( id, "hueReal", hf );
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

    DEBUGFTRACE ( "EmptyFhemQueue: done" );
}


void PushBridge::EmptyPushQueue ()
{
    DEBUGFTRACE ( "EmptyPushQueue" );

    if ( !pthread_mutex_lock ( &pushLock ) )
    {
        while ( !pushQueue.empty () ) {
            char * cmd = pushQueue.front ();
            pushQueue.pop ();
            free ( cmd );
        }

        if ( pthread_mutex_unlock ( &pushLock ) ) {
	        DEBUGV ( "EmptyPushQueue: Failed to unlock." );
        }
    }

    DEBUGFTRACE ( "EmptyPushQueue: done" );
}


void PushBridge::EnqueueToFhem ( const char * cmd )
{
    if ( !cmd ) return;

    DEBUGFTRACE ( "EnqueueToFhem" );
    if ( !enable_fhem_tunnel ) return;
        
    DEBUGFTRACE ( "EnqueueToFhem:" );

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


void PushBridge::EnqueueToPush ( char * cmd, int len )
{
    if ( !cmd ) return;

    DEBUGFTRACE ( "EnqueueToPush" );

    if ( pushQueue.size () >= 42 || len <= 12 ) return;

    char * item = strdup ( cmd + 11 );
    if ( !item ) return;

    len -= 15;
    item [ len ] = '\n';
    item [ len + 1 ] = 0;

    DEBUGFTRACE ( "EnqueueToPush: " << item );

    if ( !pthread_mutex_lock ( &pushLock ) )
    {
        if ( pushQueue.size () < 42 )
        {
            pushQueue.push ( item );
            item = 0;
        }

        if ( pthread_cond_broadcast ( &pushSignal ) ) {
	        DEBUGV ( "EnqueueToPush: Failed to signal." );
        }

        if ( pthread_mutex_unlock ( &pushLock ) ) {
	        DEBUGV ( "EnqueueToPush: Failed to unlock." );
        }
    }
    else {
	    DEBUGV ( "EnqueueToPush: Failed to lock." );
    }

    if ( item )
        free ( item );

    DEBUGFTRACE ( "EnqueueToPush: done." );
}


#ifdef USE_SSL

void sslError ( const char * arg, int res, BIO * bio )
{
	printf ( "Error [ %i ] in [ %s ]\n", res, arg );

    DEBUGFTRACE ( "sslError: [ " << res << " ]" << " [ " << arg << " ]" );

	ERR_print_errors_fp ( stderr );

	if ( bio ) { ERR_print_errors ( bio ); }
}


int CertVerifyier ( int preResult, X509_STORE_CTX * ctx )
{
	char buffer [ 1024 ];

	X509 * cert = X509_STORE_CTX_get_current_cert ( ctx );
	if ( cert ) {
		X509_NAME * issuer = X509_get_issuer_name ( cert );
		if ( issuer ) {
			X509_NAME_oneline ( issuer, buffer, 1024 );
            DEBUGFTRACE ( "CertVerifyier: Issuer [ " << buffer << " ]" );
		}

		X509_NAME * subject = X509_get_subject_name ( cert );
		if ( subject ) {
			X509_NAME_oneline ( subject, buffer, 1024 );
            DEBUGFTRACE ( "CertVerifyier: Subject [ " << buffer << " ]" );
		}
	}
	return 1; // Ignore verification process as we assume self-signed certificates ...
	//return preResult;
}


bool EstablishSSL ( SSL_CTX * &ctx, BIO * &web, SSL * &ssl, const char * host, int port )
{
	long res = 1;
	char hostAndPort [ 1024 ];

	const SSL_METHOD* method = SSLv23_method ();
	if ( !method ) {
		sslError ( "SSLv23_method", 0, 0 ); return false;
	}
	
	ctx = SSL_CTX_new ( method );
	if ( !ctx ) {
		sslError ( "SSL_CTX_new", 0, 0 ); return false;
	}

	SSL_CTX_set_verify ( ctx, SSL_VERIFY_PEER, CertVerifyier );

	SSL_CTX_set_verify_depth ( ctx, 4 );

	const long flags = SSL_OP_NO_COMPRESSION;
	SSL_CTX_set_options ( ctx, flags );

	res = SSL_CTX_load_verify_locations ( ctx, "certs.pem", NULL );
#ifdef DEBUG
	if ( res != 1 ) { sslError ( "SSL_CTX_load_verify_locations", res, 0 ); }
#endif
	web = BIO_new_ssl_connect ( ctx );
	if ( !web ) {
		sslError ( "BIO_new_ssl_connect", 0, web ); return false;
	}

	int len = snprintf ( hostAndPort, 1024, "%s:%i", host, port );
	if ( len <= 0 ) {
		printf ( "Error building host and port.\n" );
		return false;
	}

	res = BIO_set_conn_hostname ( web, hostAndPort );
	if ( res != 1 ) {
		sslError ( "BIO_set_conn_hostname", res, web ); return false;
	}
	
	BIO_get_ssl ( web, &ssl );
	if ( !ssl ) {
		sslError ( "BIO_get_ssl", 0, web ); return false;
	}

	const char* const ciphersList = "ALL";
	res = SSL_set_cipher_list ( ssl, ciphersList );
	if ( res != 1 ) {
		sslError ( "SSL_set_cipher_list", res, 0 ); return false;
	}

	res = SSL_set_tlsext_host_name ( ssl, host );
	if ( res != 1 ) {
		sslError ( "SSL_set_tlsext_host_name", res, 0 ); return false;
	}

	res = BIO_do_connect ( web );
	if ( res != 1 ) {
		sslError ( "BIO_do_connect", res, web ); return false;
	}

	res = BIO_do_handshake ( web );
	if ( res != 1 ) {
		sslError ( "BIO_do_handshake", res, web ); return false;
	}

	res = BIO_get_fd ( web, 0 );
	if ( res < 0 ) {
		sslError ( "BIO_get_fd", res, web );
	}
	else {		
		int value = 1;
		int rc = setsockopt ( res, IPPROTO_TCP, TCP_NODELAY, ( const char * ) &value, sizeof ( value ) );
		if ( rc < 0 ) {
			sslError ( "setsockopt", rc, 0 );
		}
	}

	/* Make sure that a server cert is available */
	X509* cert = SSL_get_peer_certificate ( ssl );
	if ( cert ) { 
		X509_free ( cert ); 
	}
	else {
		sslError ( "SSL_get_peer_certificate", 0, 0 ); return false;
	}
	if ( NULL == cert ) sslError ( "SSL_get_peer_certificate", 0, 0 );

	res = SSL_get_verify_result ( ssl );
#ifdef DEBUG
	if ( X509_V_OK != res ) { sslError ( "SSL_get_verify_result", 0, 0 ); }
#endif

	return true;
}

#endif


bool PushBridge::EstablishAuthSSL ( )
{
#ifdef USE_SSL
    DEBUGFTRACE ( "EstablishAuthSSL" );

	DisposeSSL ( ssl_ctx, ssl_web );

    SSL_CTX     *   ctx = ( SSL_CTX * ) ssl_ctx;
    BIO         *   web = ( BIO * ) ssl_web;
    SSL         *   ssl = ( SSL * ) ssl_ptr; 

    if ( EstablishSSL ( ctx, web, ssl, fhem_ip_addr.c_str (), fhem_port ) )
    {
        ssl_ctx = ( void * ) ctx;
        ssl_web = ( void * ) web;
        ssl_ptr = ( void * ) ssl;

        if ( fhemPassword.size () <= 0 ) {
            DEBUGFTRACE ( "EstablishAuthSSL: Connected. No password found." );
            return true;
        }

		int reads = 0; int len = 0;
                
		do
		{
			char buffer [ 2096 ] = { };

			len = BIO_read ( web, buffer, sizeof ( buffer ) );
			reads++;

			if ( len > 0 ) {
                DEBUGFTRACE ( "EstablishAuthSSL: " << buffer );

				if ( strstr ( buffer, "Password:" ) ) {
                    DEBUGFTRACE ( "EstablishAuthSSL: [ " << reads << " / " << len << " ] [ " << buffer << " ]" );

					if ( BIO_puts ( web, fhemPassword.c_str () ) <= 0 ) {
                        DEBUGFTRACE ( "Error writing password" ); break;
					}
					if ( BIO_puts ( web, "\n" ) <= 0 ) {
						DEBUGFTRACE ( "Error writing newline" ); break;
					}
					if ( BIO_flush ( web ) <= 0 ) {
						DEBUGFTRACE ( "Error flushing buffer" );
					}
                    return true;
				}
				buffer [ len ] = 0;
                DEBUGFTRACE ( "EstablishAuthSSL: [ " << reads << " / " << len << " ] [ " << buffer << " ]" );
			}
			else {
                DEBUGFTRACE ( "EstablishAuthSSL: [ " << reads << " / " << len << " ]" );
			}

            if ( reads > 30 ) {
                DEBUGFTRACE ( "EstablishAuthSSL: Giving up due to reads > 30!" ); break;
            }
		}
		while ( len > 0 || BIO_should_retry ( web ) );
    }

    DisposeSSL ( ssl_ctx, ssl_web );
#endif

    return false;
}


bool PushBridge::SendToFhemSSL ( const char * cmd )
{
    DEBUGFTRACE ( "SendToFhemSSL:" << cmd );

#ifdef USE_SSL
    bool retried = false;

Retry:
    if ( !ssl_ctx ) {
        if ( !EstablishAuthSSL () ) {
            DEBUGFTRACE ( "SendToFhemSSL: Failed to establish ssl session!" ); return false;
        }
        
        fhemListenerRun = true;

        pthread_create ( &fhemListener, 0, FhemListenerStarter, this );
    }

    size_t len = strlen ( cmd );

    DEBUGFTRACE ( "SendToFhemSSL: send to FHEM ..." );

    BIO         *   web = ( BIO * ) ssl_web;

    int bytesSent = ( int ) BIO_puts ( web, cmd );

    if ( BIO_flush ( web ) <= 0 ) {
        DEBUGFTRACE ( "SendToFhemSSL: Error flushing buffer" );
    }
    
    if ( bytesSent != ( int ) len ) {
        DEBUGV ( "SendToFhemSSL: Failed to send to FHEM." );

		fhemListenerRun = false;

        DisposeSSL ( ssl_ctx, ssl_web );

        if ( !retried ) {
            retried = true;
            goto Retry;
        }
        
        return false;
        //EmptyFhemQueue ();        
    }
        
    DEBUGV ( "SendToFhem: " << cmd );
#endif
    return true;
}


bool PushBridge::SendToFhem ( const char * cmd )
{
    if ( fhemSSL ) {
        return SendToFhemSSL ( cmd );
    }

    DEBUGFTRACE ( "SendToFhem:" << cmd );

    bool retried = false;

Retry:
    if ( fhemSocket < 0 ) {
        // Create socket
        DEBUGV ( "SendToFhem: Creating socket ..." );

        int sock = ( int ) socket ( PF_INET, SOCK_STREAM, 0 );
        if ( sock < 0 ) {
            DEBUGV ( "SendToFhem: Failed to create socket." );

            //EmptyFhemQueue ();
            return false;
        }
        
		struct sockaddr_in  addr;
        memset ( &addr, 0, sizeof ( addr ) );

		addr.sin_family		    = PF_INET;
		addr.sin_port		    = htons ( fhem_port );

        inet_aton ( fhem_ip_addr.c_str (), &addr.sin_addr );
		//addr.sin_addr.s_addr	= htonl ( INADDR_LOOPBACK );

        DEBUGV ( "SendToFhem: Connecting to FHEM ..." );

        int rc = ::connect ( sock, (struct sockaddr *) & addr, sizeof ( struct sockaddr_in ) );
        if ( rc < 0 ) {
            DEBUGV ( "SendToFhem: Failed to connect to FHEM." );

            EmptyFhemQueue ();

            ::close ( sock );
            return false;
        }
        else {
            fhemSocket = sock; fhemListenerRun = true; conectTime = time ( 0 );
        
            if ( fhemPassword.size () <= 0 ) {
                fhemAuthOK = true;
            }
            else {
                fhemAuthOK = false;
            }

            pthread_create ( &fhemListener, 0, FhemListenerStarter, this );
        }
    }
    
    if ( !fhemAuthOK ) {
        uint64_t t = time ( 0 );
        if ( conectTime - t > 4 ) {
            fhemAuthOK = true;
        }
        else {
            DEBUGFTRACE ( "SendToFhem: Awaiting authentication." );
            return false;
        }
    }

    size_t len = strlen ( cmd );

    DEBUGFTRACE ( "SendToFhem: send to FHEM ..." );

    int bytesSent = ( int ) ::send ( fhemSocket, cmd, len, MSG_NOSIGNAL );
    if ( bytesSent != ( int ) len ) {
        DEBUGV ( "SendToFhem: Failed to send to FHEM." );

		fhemListenerRun = false;

        ::close ( fhemSocket );
        fhemSocket = -1;

        if ( !retried ) {
            retried = true;
            goto Retry;
        }
        
        return false;
        //EmptyFhemQueue ();
    }
        
    DEBUGV ( "SendToFhem: " << cmd );
        
    return true;
}


void PushBridge::FhemThread ()
{
    fhemActive = true;

    DEBUGV ( "FhemThread started ..." );

#ifdef USE_SSL
    if ( fhemSSL && !fhemSSLInitialized ) {
        ( void ) SSL_library_init ();

        SSL_load_error_strings ();
        
        fhemSSLInitialized = true;
    }
#endif

    bool doUnlock = true;
    
    // Request configuration
    RequestConfig ();

    if ( !pthread_mutex_lock ( &fhemQueueLock ) )
    {
        while ( fhemThreadRun )
        {
            char * cmd = 0;

            if ( !fhemQueue.empty () ) {
                cmd = fhemQueue.front ();
            }

            if ( cmd ) {
                bool ok;

                if ( pthread_mutex_unlock ( &fhemQueueLock ) ) {
                    DEBUGV ( "FhemThread: Failed to unlock." );
                    doUnlock = false; break;
                }

                ok = SendToFhem ( cmd );

                if ( pthread_mutex_lock ( &fhemQueueLock ) ) {
                    DEBUGV ( "FhemThread: Failed to lock." );
                    doUnlock = false; break;
                }

                if ( ok ) {
                    fhemQueue.pop ();
                    free ( cmd );
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


char * getNextSep ( char * tmp )
{
    while ( 1 ) {
        char c = *tmp;
        if ( !c || c == '\n' )
            break;

        if ( c == ';' )
            return tmp;
        tmp++;
    }
    return 0;
}


void PushBridge::FhemListener ()
{
    fhemListenerActive = true;

    DEBUGV ( "FhemListener started ..." );

    char    buffer [ 1024 ];

    while ( fhemListenerRun && ( fhemSocket >= 0 || ssl_web ) )
    {
        int rc;
#ifdef USE_SSL
        if ( fhemSSL ) {
            if ( !ssl_web ) {
                DEBUGFTRACE ( "FhemListener: Closing due to invalid ssl_web!" );
                break;
            }

            BIO * web = ( BIO * ) ssl_web;

			rc = BIO_read ( web, buffer, 1023 );
            DEBUGFTRACE ( "FhemListener: ssl response length " << rc );
        }
        else
#endif
            rc = ::recv ( fhemSocket, buffer, 1023, 0 );

        if ( rc <= 0 ) {
            break;
        }

        DEBUGV ( "FhemListener: Response: " << buffer );
        DEBUGFTRACE ( "FhemListener: Response: " << buffer );

        if ( strstr ( buffer, "deCONZ_value:" ) != buffer ) {
            if ( fhemSocket >= 0 && strstr ( buffer, "Password:" ) ) {
			    int len = snprintf ( buffer, 1023, "%s\n", fhemPassword.c_str () );
                
                DEBUGV ( "SendToFhem: Sending password ..." );

                int bytesSent = ( int ) ::send ( fhemSocket, buffer, len, MSG_NOSIGNAL );
                if ( bytesSent != ( int ) len ) {
                    DEBUGV ( "SendToFhem: Failed to send password." );
                    break;
                }
                fhemAuthOK = true; SignalFhemThread ();
                continue;
            }
            continue;
        }
        

        bool restartFhemThread  = false;
        bool restartPushThread  = false;

        DEBUGV ( "FhemListener: Parsing ... " );

        char * conf = buffer + sizeof ( "deCONZ_value:" ) - 1;
        while ( 1 ) {
            char * next = getNextSep ( conf );

            if ( next ) *next = 0;

            DEBUGV ( "FhemListener: Opt " << conf );

            if ( strstr ( conf, "NR:" ) == conf ) {
                fbridge_id = atoi ( conf + sizeof("NR:")  - 1 );
                DEBUGV ( "FhemListener: fbridge_id " << fbridge_id );

                SaveCache ();
            }
            else if ( strstr ( conf, "disable:" ) == conf ) {
                DEBUGV ( "FhemListener: disable " << conf [ sizeof("disable:") - 1 ] );
                if ( conf [ sizeof("disable:") - 1 ] == '1' ) {
                    enable_plugin = false; restartFhemThread = true; restartPushThread = true;
                    break; 
                }
            }
            else if ( strstr ( conf, "disablefhem:" ) == conf ) {
                bool value = ( conf [ sizeof("disablefhem:") - 1 ] == '1' ? true : false );
                DEBUGV ( "FhemListener: disablefhem " << value );
                if ( value != enable_fhem_tunnel ) {
                    enable_fhem_tunnel = value; restartFhemThread = true;
                }
            }
            else if ( strstr ( conf, "disablepush:" ) == conf ) {
                bool value = ( conf [ sizeof("disablepush:") - 1 ] == '1' ? true : false );
                DEBUGV ( "FhemListener: disablepush " << value );
                if ( value != enable_push ) {
                    enable_push = value; restartPushThread = true;
                }
            }
            else if ( strstr ( conf, "nonodeupdate:" ) == conf ) {
                fhem_node_update = ( conf [ sizeof("nonodeupdate:") - 1 ] == '1' );
                DEBUGV ( "FhemListener: nonodeupdate " << fhem_node_update );
            }
            else if ( strstr ( conf, "fport:" ) == conf ) {
                 int value = atoi ( conf + sizeof("fport:") - 1 );
                DEBUGV ( "FhemListener: fport " << value );
                 if ( value != fhem_port ) {
                     fhem_port = value; restartFhemThread = true;
                 }
            }
            else if ( strstr ( conf, "pport:" ) == conf ) {
                 int value = atoi ( conf + sizeof("pport:") - 1 );
                 DEBUGV ( "FhemListener: pport " << value );
                 if ( value != bridge_push_port ) {
                     bridge_push_port = value; restartPushThread = true;
                 }
            }
            else if ( strstr ( conf, "ssl:" ) == conf ) {
                bool ssl = ( conf [ sizeof("ssl:") - 1 ] == '1' );
                DEBUGV ( "FhemListener: ssl " << ssl );
                if ( ssl != fhemSSL ) {
                    fhemSSL = ssl; restartFhemThread = true;
                }
            }
            else if ( strstr ( conf, "fpass:" ) == conf ) {
                string value = ( conf + sizeof("fpass:") - 1);
                //DEBUGV ( "FhemListener: fpass " << value );
                DEBUGV ( "FhemListener: fpass ..." );
                if ( fhemPassword.compare ( value ) ) {
                    fhemPassword = value;
                    restartFhemThread = true;
                    DEBUGFTRACE ( "LoadConfigFile: Using fhem new password " );
                }
            }

            if ( !next ) 
                break;

            conf = next + 1;
        }

        if ( restartPushThread ) HandlePushThread ();

        if ( restartFhemThread ) {
            HandleFhemThread ();
            break;
        }
    }

    fhemListenerActive = false;

    DEBUGV ( "FhemListener bye bye." );
}

void * PushBridge::FhemListenerStarter ( void * arg )
{
    ((PushBridge *) arg)->FhemListener ();
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

        int value = 1;
        setsockopt ( acceptSocket, SOL_SOCKET, SO_REUSEADDR, ( const char * ) &value, sizeof ( value ) );
        
#ifdef SO_REUSEPORT
        value = 1;
        setsockopt ( acceptSocket, SOL_SOCKET, SO_REUSEPORT, ( const char * ) &value, sizeof ( value ) );
#endif
        struct sockaddr_in  addr;
        memset ( &addr, 0, sizeof ( addr ) );

        addr.sin_family		    = PF_INET;
        addr.sin_port		    = htons ( bridge_push_port );
        addr.sin_addr.s_addr	= htonl ( INADDR_ANY );
        
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


void CollectQueue ( char * buffer, size_t & len, size_t remain, queue < char * > & pushQueue )
{
    if ( pushQueue.size () > 0 ) {
        DEBUGFTRACE ( "CollectQueue: Collecting " << pushQueue.size () << " ..." );

        while ( pushQueue.size () > 0 ) {
            char * cmd = pushQueue.front ();

            size_t size = strlen ( cmd );
            if ( size < remain ) {
                pushQueue.pop ();
                memcpy ( buffer + len, cmd, size );

                len += size;
                remain -= size;
                free ( cmd );
            }
            else {
                break;
            }
        }
    }
}


void PushBridge::PushThread ()
{
    pushActive = true;

    DEBUGV ( "PushThread started ..." );

    char    buffer [ 1024 ];
    size_t  len     = 0;

    while ( pushThreadRun && pushSocket != -1 )
    {
        if ( pthread_mutex_lock ( &pushLock ) ) {
            DEBUGV ( "PushThread: Failed to lock." );
            break;
        }

        if ( pushQueue.size () == 0 ) {
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
        len     = 0;

        CollectQueue ( buffer, len, 1023, pushQueue );

        if ( pthread_mutex_unlock ( &pushLock ) ) {
            DEBUGV ( "PushThread: Failed to lock." );
            break;
        }

        if ( len > 0 ) {
            DEBUGV ( "PushThread: " << buffer );

            int rc = ::send ( pushSocket, buffer, len, MSG_NOSIGNAL );
            if ( rc > 0 ) {
                rc = ::recv ( pushSocket, buffer, 10, MSG_DONTWAIT );
                if ( rc > 0 && ( strstr ( buffer, "quit" ) == buffer || strstr ( buffer, "exit" ) == buffer ) ) {
                    ::shutdown ( pushSocket, 2 );
                    ::close ( pushSocket );
                    pushSocket = -1;
                    break;
                }
            }
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

PushDevice::PushDevice ( uint64_t deviceMac, char _dtype, int _id, const char * _name ) {
    SetIdentity ( deviceMac, _dtype, _id, _name );
}


PushDevice::~PushDevice () {}


void PushDevice::SetIdentity ( uint64_t deviceMac, char _dtype, int _id, const char * _name )
{
    DEBUGFTRACE ( "PushDevice::SetIdentity" );
    
    available   = false;

    id      = _id;
    dtype   = _dtype;
    name    = _name;
    node    = 0;
    mac     = deviceMac;

    needsNodeUpdate = true;
}


void PushBridge::UpdatePushDevice ( PushDevice * device )
{
    DEBUGFTRACE ( "UpdatePushDevice" );

    if ( !device ) return;

    if ( device->needsNodeUpdate && device->id > 0 ) 
    {
        Node * node = ( Node * ) device->node;
        if ( node )
        {
            device->needsNodeUpdate = false;

            char cmd [ 512 ];

			const NodeDescriptor & desc = node->nodeDescriptor ();

			int len = snprintf ( cmd, 512, "{pushupd1('%i^%c^%i^userDescriptor^%s^"
				"deviceTypeString^%s^"
				"manufacturerCode^%i^deviceType^%i^"
				"macCapabilities^%i^"
				"isFullFunctionDevice^%i^"
				"isMainsPowered^%i^"
				"receiverOnWhenIdle^%i^"
				"securitySupport^%i^"
				"frequencyBandString^%s"
				"')}\n",
                fbridge_id, device->dtype, device->id,
				qPrintable ( node->userDescriptor () ),
				qPrintable ( node->deviceTypeString () ),
				desc.manufacturerCode (),
				( int ) desc.deviceType (),
				( int ) desc.macCapabilities (),
				desc.isFullFunctionDevice () ? 1 : 0,
				desc.isMainsPowered () ? 1 : 0,
				desc.receiverOnWhenIdle () ? 1 : 0,
				desc.securitySupport () ? 1 : 0,
				desc.frequencyBandString ()
			);
            
            if ( fhemActive && fbridge_id > 0 )
                EnqueueToFhem ( cmd );
            
            if ( pushActive ) {
                EnqueueToPush ( cmd, len );
            }
        }
    }
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

				DEBUGV1 ( "PushDevice updating the list with node, mac:" << mac << " device:" << device->dtype << device->id );
			}
#ifdef DEBUG1
			else {
				DEBUGV1 ( "PushDevice already complete mac:" << mac << " device:" << device->dtype << device->id );
			}
#endif
			UpdatePushDevice ( device );
			return true;
		}
        DEBUGL ( else { DEBUGV2 ( "UpdatePushDevice: PushDevice NOT found! mac:" << mac ); } )
	}

    return false;
}


PushDevice * PushBridge::GetPushDevice ( uint64_t mac, bool update )
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
        else {
            if ( exportedRestPlugin ) 
            {
                int id = -1;
                char type = 0;
                const char * name = "Unknown";

                deCONZ::Address addr;
                addr.setExt ( mac );

                LightNode * lightNode = exportedRestPlugin->getLightNodeForAddress ( addr );
                if ( lightNode ) {
                    lightNode->m_pushDType = type = 'l';
                    id = lightNode->id ().toInt ();
                    name = qPrintable ( lightNode->name () );
                }
                else {
                    Sensor * sensor = exportedRestPlugin->getSensorNodeForAddress ( addr );
                    if ( sensor ) {
                        sensor->m_pushDType = type = 's';
                        id = sensor->id ().toInt ();
                        name = qPrintable ( sensor->name () );
                    }
                }

                if ( id > 0 && type ) {
                    device = new PushDevice ( mac, type, id, name );
                    if ( device ) {
                        devices [ mac ] = device;
                    }
                }
            }
        }

		if ( pthread_mutex_unlock ( &devicesLock ) ) {
			DEBUGV ( "GetPushDevice: Failed to unlock." );
		}

		if ( device ) {
			DEBUGV2 ( "GetPushDevice: mac " << mac << " found. type:" << device->dtype );

            if ( update && fbridge_id >= 0 && device->id > 0 && ( __sync_val_compare_and_swap ( &device->available, false, true ) == false ) )
			{
				if ( fhem_node_update ) {
					const Node * node = ( Node * ) device->node;
					if ( node ) {
						DEBUGV2 ( "GetPushDevice: Updating node. mac:" << device->mac << " device:" << device->dtype << device->id );

						apsInst->updateNode ( *node );
					}
					else {
						DEBUGV ( "GetPushDevice: device:" << device->dtype << device->id << " node is MISSING!" );
					}
				}

				char cmd [ 128 ];

				DEBUGV2 ( "GetPushDevice: setreading reachable mac:" << mac << " or name:" << device->dtype << device->id );
				DEBUGV ( "GetPushDevice: setreading reachable mac:" << mac << " or name:" << device->dtype << device->id );
                
	            int len = snprintf ( cmd, 128, "{pushupd1('%i^%c^%i^reachable^1')}\n", fbridge_id, device->dtype, device->id );

                if ( fhemActive && fbridge_id > 0 )
				    EnqueueToFhem ( cmd );
                
                if ( pushActive ) {
                    EnqueueToPush ( cmd, len );
                }
			}
		}
		else {
			DEBUGV ( "GetPushDevice: mac " << mac << " NOT found!" );		
		}
	}
    return device;
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
    
    const char * name = 0;

    if ( !fhemActive && !pushActive ) return;
        
    PushDevice * device = GetPushDevice ( addr );

    if ( device && device->id > 0 ) {
        const char * data = ind.asdu ().data ();
        if ( data ) 
        {
            int size = ind.asdu ().size ();

            if ( device->dtype == 's' ) 
            {
                DEBUGV1 ( "apsdeDataIndication: isSwitch 1 size:" << size << " [ " << device->name.c_str () << " ]" );
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

                    if ( btn >= 0 && fbridge_id >= 0 ) {
                        DEBUGV ( "apsdeDataIndication: btn:" << btn );
                        DEBUGV2 ( "apsdeDataIndication: btn:" << btn );
                        int seq = ( int ) data [ 1 ];

                        char cmd [ 128 ];
	                    int len = snprintf ( cmd, 128, "{pushupd1('%i^%c^%i^button%i^%i^buttonLast^%i')}\n", fbridge_id, device->dtype, device->id, btn, seq, btn );

                        if ( fhemActive && fbridge_id > 0 )
                            EnqueueToFhem ( cmd );
                        
                        if ( pushActive ) {
                            EnqueueToPush ( cmd, len );
                        }
                    }
#ifdef DEBUG
                    else {
                        DEBUGV1 ( "apsdeDataIndication: button unrecognized. size:" << size << " [ " << device->name.c_str () << " ]" );

                        const char * hex = ConvertToHexSpaceString ( data, (unsigned) size );                
                        logfile1 << "\t" << hex << endl;
                    }
#endif                    
                }
            }
            else {
                if ( size > 0 ) {
                    if ( data [ 0 ] == 0 && device->dtype == 'l' ) {
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

#ifdef DEBUG
    if ( device ) {
        DEBUGV1 ( device->dtype << device->id << " Indication eps:" << (int) ind.srcEndpoint () << " epd:" << (int) ind.dstEndpoint () << " [ " << device->name.c_str () << " ]" );
    }
    else {
        DEBUGV1 ( " Unknown Indication eps:" << (int) ind.srcEndpoint () << " epd:" << (int) ind.dstEndpoint ()  );        
    }
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
    }
    DEBUGL ( else { name = addrs; } )
#endif            
    
    DEBUGV1 ( name << " Confirm dst " << " id:" << (int) conf.id () << " status:" << (int) conf.status () << " eps:" << (int) conf.srcEndpoint () << " epd:" << (int) conf.dstEndpoint () << endl );
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

    DEBUGL ( const char * name = "Unknown"; )
    
    PushDevice * device = 0;

    if ( !raspBee.mac && node->isCoordinator () ) {
        raspBee.mac     = addr;
        raspBee.node    = node;
        DEBUGL ( device = &raspBee; )

        SaveCache ();
    }
    else {
        device = GetPushDevice ( addr, updEvent );        
    }

    if ( device ) {
        DEBUGL ( name    = device->name.c_str (); )
    }
    DEBUGL ( else { name    = ( addr ?  ( qPrintable ( node->address ().toStringExt () ) ) : "0x0000" ); } )

    DEBUGL ( unsigned int evi = ( unsigned int ) ev; )
    
    DEBUGL ( const char * ename = "Invalid"; )
    DEBUGL ( if ( evi < MAX_EVENT_NAME ) ename = eventNames [ evi ]; )

    DEBUGV1 ( name << " Event:" << ename << endl );

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


bool RestNodeBase::UpdateToFhem ()
{
    DEBUGFTRACE ( "RestNodeBase::UpdateToFhem" );

    if ( !needsRestUpdate ) { return true; }

    if ( !pushBridge.fhemActive && !pushBridge.pushActive ) {
        return false;
    }

    int id = this->id ().toInt ();
    if ( id <= 0 ) { return false; }

    char cmd [ 256 ];

    int len = snprintf ( cmd, 256, "{pushupd1('%i^%c^%i^id^%i^"
        "uniqueid^%s^"
        "available^%i"
#ifdef DEBUG
        "^mgmtBindSupported^%i^"
        "read^%i^"
        "lastAttributeReportBind^%i"
#endif
        "')}\n"
        ,
        pushBridge.fbridge_id, m_pushDType, id, id,
        qPrintable ( m_uid ),
        ( int ) m_available
#ifdef DEBUG
        ,
        ( int ) m_mgmtBindSupported,
        ( int ) m_read,
        ( int ) m_lastAttributeReportBind
#endif
    );

    if ( pushBridge.fhemActive && pushBridge.fbridge_id > 0 )
        pushBridge.EnqueueToFhem ( cmd );
                        
    if ( pushBridge.pushActive ) {
        pushBridge.EnqueueToPush ( cmd, len );
    }

    return true;
}


bool LightNode::UpdateToFhem ()
{
    DEBUGFTRACE ( "LightNode::UpdateToFhem" );

    m_pushDType = 'l';

    if ( !needsRestUpdate ) { return true; }

    if ( m_type.contains ( "plug" ) ) {
        m_pushType = PUSH_TYPE_PLUG;
    }

    int id = this->id ().toInt ();
    if ( id <= 0 ) { return false; }

    if ( !pushBridge.fhemActive && !pushBridge.pushActive ) {
        return false;
    }

    char cmd [ 1024 ];

    int pct = m_isOn ? ( ( m_level * 100 ) / 253 ) : 0;
    
    int len = snprintf ( cmd, 1024, "{pushupd1('%i^l^%i^level^%d^"
        "bri^%d^"
        "pct^%d"
        "')}\n",
        pushBridge.fbridge_id, id, 
        ( int ) m_level,
        ( int ) m_level,
        ( int ) pct
    );
    
    if ( pushBridge.fhemActive && pushBridge.fbridge_id > 0 )
        pushBridge.EnqueueToFhem ( cmd );
                        
    if ( pushBridge.pushActive ) {
        pushBridge.EnqueueToPush ( cmd, len );
    }

	if ( !m_manufacturer.length () || !m_modelId.length () || !m_swBuildId.length () || !m_type.length () ) {
		return false;
	}
    
    len = snprintf ( cmd, 1024, "{pushupd1('%i^l^%i^manufacturer^%s^"
        "modelid^%s^"
        "swversion^%s^"
        "type^%s^"
        "manufacturerCode^%i^"
        "clusterId^%i^"
#ifdef DEBUG
        "resetRetryCount^%i^"
        "zdpResetSeq^%i^"
#endif
        "groupCapacity^%i^"
        "on^%i^"
        "groupCount^%i^"
        "sceneCapacity^%i"
        "')}\n",
        pushBridge.fbridge_id, id, qPrintable ( m_manufacturer ),
        qPrintable ( m_modelId ),
        qPrintable ( m_swBuildId ),
        qPrintable ( m_type ),
        ( int ) m_manufacturerCode,
        ( int ) m_otauClusterId,
#ifdef DEBUG
        ( int ) m_resetRetryCount,
        ( int ) m_zdpResetSeq,
#endif
        ( int ) m_groupCapacity,
        ( int ) m_isOn,
        ( int ) m_groupCount,
        ( int ) m_sceneCapacity
    );

    if ( pushBridge.fhemActive && pushBridge.fbridge_id > 0 )
        pushBridge.EnqueueToFhem ( cmd );
                        
    if ( pushBridge.pushActive ) {
        pushBridge.EnqueueToPush ( cmd, len );
    }
    
    bool baseOK = RestNodeBase::UpdateToFhem ();

    if ( m_pushType != PUSH_TYPE_PLUG )
    {
		/*num R, G, B;
		num X = m_colorX, Y = m_colorY, Z = 1;

		Xyz2Rgb ( &R, &G, &B, X, Y, Z );

		unsigned int rgb = 0;
		rgb |= ( unsigned int ) R; rgb <<= 8;
		rgb |= ( unsigned int ) G; rgb <<= 8;
		rgb |= ( unsigned int ) B;*/
		
        len = snprintf ( cmd, 1024, "{pushupd1('%i^l^%i^hascolor^%i^"
            "hue^%i^"
            "ehue^%i^"
            "normHue^%f^"
            "sat^%i^"
            "colorX^%i^"
            "colorY^%i^"
            "colorTemperature^%i^"
            "colormode^%s^"
            "colorLoopActive^%i^"
            "colorLoopSpeed^%i"
            "')}\n",
            pushBridge.fbridge_id, id, m_hasColor ? 1 : 0,
            ( int ) m_hue,
            ( int ) m_ehue,
            m_normHue,
            ( int ) m_sat,
            ( int ) m_colorX,
            ( int ) m_colorY,
            ( int ) m_colorTemperature,
            m_colorMode.length () ? qPrintable ( m_colorMode ) : "undef",
            m_colorLoopActive ? 1 : 0,
            m_colorLoopSpeed
        );

        if ( pushBridge.fhemActive && pushBridge.fbridge_id > 0 )
            pushBridge.EnqueueToFhem ( cmd );
                            
        if ( pushBridge.pushActive ) {
            pushBridge.EnqueueToPush ( cmd, len );
        }
    }

    if ( baseOK ) {
        needsRestUpdate = false;
        return true;
    }
    return false;
}


bool Sensor::UpdateToFhem ()
{
    DEBUGFTRACE ( "Sensor::UpdateToFhem" );

    m_pushDType = 's';

    if ( !needsRestUpdate ) { return true; }

    int id = this->id ().toInt ();
    if ( id <= 0 ) { return false; }

    if ( !pushBridge.fhemActive && !pushBridge.pushActive ) {
        return false;
    }

    char cmd [ 512 ];

    if ( !m_name.length () || !m_type.length () || !m_modelid.length () || !m_manufacturer.length ()
            || !m_swversion.length () ) {
        return false;
    }

    int len = snprintf ( cmd, 512, "{pushupd1('%i^s^%i^deletedstate^%i^"
        "name^%s^"
        "type^%s^"
        "modelid^%s^"
        "manufacturer^%s^"
        "swversion^%s^"
        "mode^%i^"
#ifdef DEBUG
        "resetRetryCount^%i^"
        "zdpResetSeq^%i^"
#endif
        "etag^%s"
        "')}\n",
        pushBridge.fbridge_id, id, ( int ) m_deletedstate,
        qPrintable ( m_name ),
        qPrintable ( m_type ),
        qPrintable ( m_modelid ),
        qPrintable ( m_manufacturer ),
        qPrintable ( m_swversion ),
        m_mode,
#ifdef DEBUG
        m_resetRetryCount,
        m_zdpResetSeq,
#endif
        etag.length () ? qPrintable ( etag ) : "undef"
    );

    if ( pushBridge.fhemActive && pushBridge.fbridge_id > 0 )
        pushBridge.EnqueueToFhem ( cmd );
                        
    if ( pushBridge.pushActive ) {
        pushBridge.EnqueueToPush ( cmd, len );
    }

    m_state.m_pushId = id;
    m_state.UpdateToFhem ();

    m_config.m_pushId = id;
    m_config.UpdateToFhem ();  

    if ( RestNodeBase::UpdateToFhem () ) {
        needsRestUpdate = false;
        return true;
    }
    return false;
}


bool Sensor::UpdateSensorFhem ()
{
    if ( !pushBridge.enable_plugin ) { return false; }

    DEBUGFTRACE ( "Sensor::UpdateSensorFhem" );
    
    if ( needsRestUpdate ) {
        return UpdateToFhem ();
    }
    return true;
}


bool SensorState::UpdateToFhem ()
{
    DEBUGFTRACE ( "SensorState::UpdateToFhem" );

    if ( m_pushId < 0 ) { return true; }

    if ( !pushBridge.fhemActive && !pushBridge.pushActive ) {
        return false;
    }

    char cmd [ 512 ];

    int len = snprintf ( cmd, 512, "{pushupd1('%i^s^%i^lastupdated^%s^"
        "flag^%s^"
        "status^%s^"
        "open^%s^"
        "lux^%i"
        "')}\n",
        pushBridge.fbridge_id, m_pushId, m_lastupdated.length () ? qPrintable ( m_lastupdated ) : "undef",
        m_flag.length () ? qPrintable ( m_flag ) : "undef",
        m_status.length () ? qPrintable ( m_status ) : "undef",
        m_open.length () ? qPrintable ( m_open ) : "undef",
        m_lux
    );

    if ( pushBridge.fhemActive && pushBridge.fbridge_id > 0 )
        pushBridge.EnqueueToFhem ( cmd );
                        
    if ( pushBridge.pushActive ) {
        pushBridge.EnqueueToPush ( cmd, len );
    }

    if ( m_presence.length () || m_temperature.length () || m_humidity.length () || m_daylight.length () )
    {
        len = snprintf ( cmd, 512, "{pushupd1('%i^s^%i^presence^%s^"
            "temperature^%s^"
            "humidity^%s^"
            "daylight^%s"
            "')}\n",
            pushBridge.fbridge_id, m_pushId, m_presence.length () ? qPrintable ( m_presence ) : "undef",
            m_temperature.length () ? qPrintable ( m_temperature ) : "undef",
            m_humidity.length () ? qPrintable ( m_humidity ) : "undef",
            m_daylight.length () ? qPrintable ( m_daylight ) : "undef"
        );

        if ( pushBridge.fhemActive && pushBridge.fbridge_id > 0 )
            pushBridge.EnqueueToFhem ( cmd );
                            
        if ( pushBridge.pushActive ) {
            pushBridge.EnqueueToPush ( cmd, len );
        }
    }

    return true;
}


bool SensorConfig::UpdateToFhem ()
{
    DEBUGFTRACE ( "SensorConfig::UpdateToFhem" );

    if ( m_pushId < 0 ) { return true; }

    if ( !pushBridge.fhemActive && !pushBridge.pushActive ) {
        return false;
    }

    char cmd [ 512 ];

    DEBUGFTRACE ( "SensorConfig::UpdateToFhem 1" );

    int len = snprintf ( cmd, 512, "{pushupd1('%i^s^%i^on^%i^"
        "reachable^%i^"
        "duration^%f^"
        "battery^%i^"
        "url^%s"
        "')}\n",
        pushBridge.fbridge_id, m_pushId, m_on ? 1 : 0,
        m_reachable ? 1 : 0,
        m_duration,
        m_battery,
        m_url.length () ? qPrintable ( m_url ) : "undef"
    );

    if ( pushBridge.fhemActive && pushBridge.fbridge_id > 0 )
        pushBridge.EnqueueToFhem ( cmd );
                        
    if ( pushBridge.pushActive ) {
        pushBridge.EnqueueToPush ( cmd, len );
    }
    
    if ( m_long.length () || m_lat.length () || m_sunriseoffset.length () || m_sunsetoffset.length () )
    {
        len = snprintf ( cmd, 512, "{pushupd1('%i^s^%i^longitude^%s^"
            "latitude^%s^"
            "sunriseoffset^%s^"
            "sunsetoffset^%s"
            "')}\n",
            pushBridge.fbridge_id, m_pushId, m_long.length () ? qPrintable ( m_long ) : "undef",
            m_lat.length () ? qPrintable ( m_lat ) : "undef",
            m_sunriseoffset.length () ? qPrintable ( m_sunriseoffset ) : "undef",
            m_sunsetoffset.length () ? qPrintable ( m_sunsetoffset ) : "undef"
        );

        if ( pushBridge.fhemActive && pushBridge.fbridge_id > 0 )
            pushBridge.EnqueueToFhem ( cmd );
                            
        if ( pushBridge.pushActive ) {
            pushBridge.EnqueueToPush ( cmd, len );
        }
    }

    return true;
}

#endif