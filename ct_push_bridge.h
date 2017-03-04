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

#ifndef CTPUSH_PLUGIN_H
#define CTPUSH_PLUGIN_H

#include <QString>
#include <queue>
#include <deconz.h>
#include <fstream>
#include <pthread.h>

using namespace deCONZ;

#define PUSH_TYPE_UNKNOWN       0
#define PUSH_TYPE_SWITCH        1
#define PUSH_TYPE_MOTION        2
#define PUSH_TYPE_CONTROLLER    3
#define PUSH_TYPE_PLUG          4
#define PUSH_TYPE_LIGHT         5

////////////////////////////////////////////////////////////////////////////////
// ENABLE_PUSH symbol. Disable symbol to skip compiling push bridge code
////////////////////////////////////////////////////////////////////////////////
#define ENABLE_PUSH


#ifdef ENABLE_PUSH

////////////////////////////////////////////////////////////////////////////////
// DEBUG symbol. Disable symbol to skip compiling debug log code
////////////////////////////////////////////////////////////////////////////////
#ifndef QT_NO_DEBUG 
#define DEBUG
#else // toremove
//#define DEBUG // toremove
#endif
//#define DEBUG

#ifdef DEBUG
#   define DEBUGL(a)    a
#   define DEBUGV(a)    logfile << GetTimeString () << a << endl
#   define DEBUGV1(a)   logfile1 << GetTimeString () << a << endl
#   define DEBUGV10(a)  logfile1 << a
#   define DEBUGV2(a)   DEBUGV1(a)
#   define DEBUGV20(a)  DEBUGV10(a)
#   define DEBUGFTRACE(a)   pushBridge.logfile1 << GetTimeString () << a << std::endl

const char * ConvertToHexSpaceString ( const char * src, unsigned int length );

size_t FormatTimeString ( char * timeBuffer, unsigned int bufferSize );
const char * GetTimeString ();
const char * GetNodeType ( int type );

#else	// !DEBUG
#   define DEBUGL(a)
#   define DEBUGV(a)
#   define DEBUGV1(a)
#   define DEBUGV10(a)  
#   define DEBUGV2(a) 
#   define DEBUGV20(a)
#   define DEBUGFTRACE(a)
#endif	// DEBUG


class PushDevice
{
public:
    PushDevice () { SetIdentity ( 0, 'u', -1, "Unknown" ); };

    PushDevice ( uint64_t deviceMac, char _dtype, int _id, const char * _name );
    ~PushDevice();

    void SetIdentity ( uint64_t deviceMac, char _dtype, int _id, const char * _name );

    int             id;
    char            dtype;
    uint64_t        mac;
    std::string     name;
    const Node *    node;
    bool            needsNodeUpdate;
    bool            available;
};


class PushGroup
{
public:
    PushGroup () : on ( false ), colorX ( 0 ), colorY ( 0 ), hue ( 0 ), hueFloat ( 0 ), sat ( 0 ), level ( 0 ), 
                colorTemperature ( 0 )
                {};
    ~PushGroup();

    bool            on;
    uint16_t        colorX;
    uint16_t        colorY;
    uint16_t        hue;
    float           hueFloat;
    uint16_t        sat;
    uint16_t        level;
    uint16_t        colorTemperature;
    std::string     etag;

    void            Update ( QString & id, bool o, uint16_t x, uint16_t y, uint16_t h, float hf, uint16_t s, uint16_t l, uint16_t temp );
};


class RestNodeBase;

/*
 * class PushBridge
 */
class PushBridge : public QObject
{
    Q_OBJECT

public:    
    // Construct / Destruct
    PushBridge ();
    ~PushBridge();

    void                        SetNode	            ( deCONZ::Node * denode );

    void                        SetNodeAvailable	( char type, int id, bool available );
    void                        SetNodeId			( char type, int id );
    void                        SetNodeUid			( char type, int id, const QString & m_uid );
    void                        SetNodeInfo			( char type, int id, const char * reading, const QString & value );
    void                        SetNodeInfo			( char type, int id, const char * reading, QString & value );
    void                        SetNodeInfo			( char type, int id, const char * reading, uint32_t value );
    void                        SetNodeInfoDouble   ( char type, int id, const char * reading, double value );
    void                        SetNodeState		( RestNodeBase * rnode, char type, int id, bool value, int pct );

	void                        SetGroupInfo		( QString & id, const char * reading, uint32_t value );
	void                        SetGroupInfo		( QString & id, const char * reading, float value );
	void                        SetGroupInfo		( QString & id, const char * reading, const char * value );

    void                        EnqueueToFhem ( const char * cmd );
    void                        EnqueueToPush ( char * cmd, int len );
    
public Q_SLOTS:
    void apsdeDataIndication(const deCONZ::ApsDataIndication &ind);
    void apsdeDataConfirm(const deCONZ::ApsDataConfirm &conf);
    void nodeEvent              ( const deCONZ::NodeEvent & event );

private:
    int                         fhemSocket; // Telnet connection to fhem
    std::queue < char * >       fhemQueue;
    pthread_t                   fhemThread;
    pthread_t                   fhemListener;
    pthread_mutex_t             fhemQueueLock;    
    pthread_cond_t              fhemSignal;
    bool                        fhemThreadRun;
    bool                        fhemListenerRun;
    
    void                        RequestConfig ();
    void                        LoadConfigFile ();
    void                        SaveCache ();
    void                        HandleFhemThread ();
    void                        HandlePushThread ();

    void                        EmptyFhemQueue ();
    void                        SignalFhemThread ();
    bool                        SendToFhem ( const char * cmd );
    bool                        SendToFhemSSL ( const char * cmd );
    bool                        EstablishAuthSSL ();
    void				        FhemThread ();
    static void		*	        FhemThreadStarter ( void * arg ); 
    void				        FhemListener ();
    static void		*	        FhemListenerStarter ( void * arg ); 

    int                         acceptSocket;
    pthread_t                   acceptThread;
    bool                        acceptThreadRun;

    void                        EmptyPushQueue ();
    std::queue < char * >       pushQueue;

    void				        AcceptThread ();
    static void		*	        AcceptThreadStarter ( void * arg ); 

    int                         pushSocket;
    pthread_t                   pushThread;
    bool                        pushThreadRun;
    pthread_mutex_t             pushLock;    
    pthread_cond_t              pushSignal;
    void                        SignalPushThread ();
    void				        PushThread ();
    static void		*	        PushThreadStarter ( void * arg ); 

    ApsController *             apsInst;

	pthread_mutex_t             groupLock;
	pthread_mutex_t             devicesLock;
    std::map < uint64_t, PushDevice * > devices;
    std::map < int, PushGroup * > groups;

public:
    bool                        enable_plugin;
    bool                        enable_fhem_tunnel;
    bool                        enable_push;

    bool                        fhemActive;
    bool                        fhemListenerActive;
    bool                        acceptActive;
    bool                        pushActive;
    bool                        fhemSSL;
    bool                        fhemSSLInitialized;
    uint64_t                    conectTime;
    bool                        fhemAuthOK;
    std::string                 fhemPassword;
    void                    *   ssl_ctx;
    void                    *   ssl_web;
    void                    *   ssl_ptr;

    bool                        fhem_node_update;
    std::string                 fhem_ip_addr;
    int                         fhem_port;
    int                         bridge_push_port;
    int                         fbridge_id;

    const char *                GetPushName		( uint64_t mac );
    PushDevice *                GetPushDevice	( uint64_t mac, bool update = true );

    bool                        UpdatePushDevice ( uint64_t mac, const deCONZ::Node * node );
    void                        UpdatePushDevice ( PushDevice * device );

    PushGroup *                 GetPushGroup	( QString & id );

    DEBUGL ( std::ofstream logfile; )
    DEBUGL ( std::ofstream logfile1; )
};

extern PushBridge pushBridge;

#define pushClassMethod()				virtual bool UpdateToFhem ();
#define pushClassMethodSensor()		    pushClassMethod () bool UpdateSensorFhem ();
#define pushClassExt()					pushClassMethod () char m_pushDType; bool needsRestUpdate; bool needsInitialUpdate; int m_pushId; 
#define pushClassSensorExt()			pushClassExt () 
#define pushNodeExt()					pushClassExt () int m_pushType;

#define pushClassVarsInit()				m_pushDType ( 'l' ), needsRestUpdate ( true ), needsInitialUpdate ( true ), m_pushId ( -1 ),
#define pushSensorVarsInit()			pushClassVarsInit()  
#define pushNodeVarsInit()				pushClassVarsInit() m_pushType ( PUSH_TYPE_UNKNOWN ),

#define pushCode(code)					code

#define pushIDUpd()                     if ( m_pushId <= 0 ) { m_pushId = id ().toInt (); }
#define pushIDUpd1()                    if ( m_pushId <= 0 ) { m_pushId = m_id.toInt (); }
#define pushIDUpd2()                    if ( m_pushId <= 0 || m_pushId != m_id.toInt () ) { m_pushId = m_id.toInt (); }
#define pushSetNodeState(s,l)			if ( pushBridge.enable_plugin ) { pushIDUpd() pushBridge.SetNodeState ( this, m_pushDType, m_pushId, s, l ); }
#define pushSetNodeId(a,v)		        if ( pushBridge.enable_plugin ) { pushIDUpd2() if ( a != v ) { a = v; pushBridge.SetNodeId ( m_pushDType, m_pushId ); } return; }
#define pushSetNodeUid(a,v)		        if ( pushBridge.enable_plugin ) { pushIDUpd1() if ( a != v ) { a = v; pushBridge.SetNodeUid ( m_pushDType, m_pushId, uid ); } return; }
#define pushSetNodeInfo(v)				if ( pushBridge.enable_plugin ) { pushIDUpd()pushBridge.SetNodeInfo ( m_pushDType, m_pushId, #v, v ); }
#define pushSetNodeInfoName(v,n)		if ( pushBridge.enable_plugin ) { pushIDUpd()pushBridge.SetNodeInfo ( m_pushDType, m_pushId, n, v ); }
#define pushSetNodeInfoComp(a,v)		if ( pushBridge.enable_plugin ) { pushIDUpd()if ( a != v ) { a = v; pushBridge.SetNodeInfo ( m_pushDType, m_pushId, #v, v ); } return; }
#define pushSetNodeInfoCompName(a,v,n)	if ( pushBridge.enable_plugin ) { pushIDUpd()if ( a != v ) { a = v; pushBridge.SetNodeInfo ( m_pushDType, m_pushId, n, v ); } return; }
#define pushSetNodeInfoCompName1(a,v,n)	if ( pushBridge.enable_plugin && a != v ) { pushIDUpd() pushBridge.SetNodeInfo ( m_pushDType, m_pushId, n, v ); }
#define pushSetNodeInfoMem(n)			if ( pushBridge.enable_plugin ) { pushIDUpd() pushBridge.SetNodeInfo ( m_pushDType, m_pushId, #n, m_##n ); }
#define pushSetNodeInfoMemComp(v)		if ( pushBridge.enable_plugin ) { if ( m_##v != v ) { m_##v = v; pushIDUpd() pushBridge.SetNodeInfo ( m_pushDType, m_pushId, #v, v ); } return; }
#define pushSetNodeInfoMemComp1(v)		if ( pushBridge.enable_plugin && m_##v != v ) { pushIDUpd() pushBridge.SetNodeInfo ( m_pushDType, m_pushId, #v, v ); }
#define pushSetNodeInfoMemCompDouble(v)	if ( pushBridge.enable_plugin ) { if ( m_##v != v ) { m_##v = v; pushIDUpd() pushBridge.SetNodeInfoDouble ( m_pushDType, m_pushId, #v, v ); } return; }

#define pushSetNodeInfoCompNoDev(a,v)	if ( pushBridge.enable_plugin ) { if ( a != v ) { a = v; pushIDUpd() pushBridge.SetNodeInfo (  m_name, 0, #v, v ); } return; }

#define pushSetSensorStateInfo(v)		    if ( pushBridge.enable_plugin ) pushBridge.SetNodeInfo ( 's', m_pushId, #v, v )
#define pushSetSensorStateInfoMem(n)        if ( pushBridge.enable_plugin ) pushBridge.SetNodeInfo ( 's', m_pushId, #n, m_##n )
#define pushSetSensorStateInfoMemComp(v)    if ( pushBridge.enable_plugin ) { if ( m_##v != v ) { m_##v = v; pushBridge.SetNodeInfo ( 's', m_pushId, #v, v ); } return; }

#define pushSetSensorConfigState(s,l)       if ( pushBridge.enable_plugin ) pushBridge.SetNodeState ( 0, 's', m_pushId, s, l )
#define pushSetSensorConfigInfo(v)		    if ( pushBridge.enable_plugin ) pushBridge.SetNodeInfo ( 's', m_pushId, #v, v )
#define pushSetSensorConfigInfoName(v,n)    if ( pushBridge.enable_plugin ) pushBridge.SetNodeInfo ( 's', m_pushId, n, v )
#define pushSetSensorConfigInfoMemComp(v)   if ( pushBridge.enable_plugin ) { if ( m_##v != v ) { m_##v = v; pushBridge.SetNodeInfo ( 's', m_pushId, #v, v ); } return; }
#define pushSetSensorConfigInfoMemCompDouble(v)	if ( pushBridge.enable_plugin ) { if ( m_##v != v ) { m_##v = v; pushBridge.SetNodeInfoDouble ( 's', m_pushId, #v, v ); } return; }

#define pushSetGroupInfo(v)				if ( pushBridge.enable_plugin ) pushBridge.SetGroupInfo ( m_id, #v, v )
#define pushSetGroupInfoComp(v)			if ( pushBridge.enable_plugin ) { if ( m_##v != v ) { m_##v = v; pushBridge.SetGroupInfo ( m_id, #v, v ); } return; }
#define pushSetGroupInfoComp1(v)		if ( pushBridge.enable_plugin ) { if ( m_##v != v ) { m_##v = v; pushBridge.SetGroupInfo (  m_id, #v, v ); } }
#define pushSetGroupInfoCompUI32(v)		if ( pushBridge.enable_plugin ) { if ( m_##v != v ) { m_##v = v; pushBridge.SetGroupInfo ( m_id, #v, ( uint32_t ) v ); } return; }
#define pushSetGroupInfoCompName(m,v)	if ( pushBridge.enable_plugin ) { if ( m_##m != v ) { m_##m = v; pushBridge.SetGroupInfo ( m_id, #m, v ); } return; }
#define pushSetGroupInfoCompName1(m,v)	if ( pushBridge.enable_plugin && m_##m != v )  { m_##m = v; pushBridge.SetGroupInfo ( m_id, #m, v ); }
#define pushSetGroupInfoMem(v)			if ( pushBridge.enable_plugin ) pushBridge.SetGroupInfo ( m_id, #v, m_##v )
#define pushUpdateGroupInfo()			if ( pushBridge.enable_plugin ) {\
                                            PushGroup * group = pushBridge.GetPushGroup ( m_id );\
                                            if ( group  ) {\
                                                group->Update ( m_id, m_on, colorX, colorY, hue, (float) hueReal, sat, level, colorTemperature );\
                                        } }

#else	// !ENABLE_PUSH

#define pushClassMethod()
#define pushClassMethodSensor()
#define pushClassExt()			
#define pushClassSensorExt()
#define pushNodeExt()

#define pushClassVarsInit()				
#define pushNodeVarsInit()				

#define pushCode(code)

#define pushSetNodeState(s,l) 
#define pushSetNodeInfo(v)
#define pushSetNodeInfoName(v,n)
#define pushSetNodeInfoComp(a,v) 
#define pushSetNodeInfoCompName(a,v,n)
#define pushSetNodeInfoCompName1(a,v,n)
#define pushSetNodeInfoMem(n)
#define pushSetNodeInfoMemComp(n)
#define pushSetNodeInfoMemComp1(n)
#define pushSetNodeInfoMemCompDouble(v)	

#define pushSetNodeInfoCompNoDev(a,v)

#define pushSetSensorStateInfo(v)	
#define pushSetSensorStateInfoMem(n)
#define pushSetSensorStateInfoMemComp(v)	

#define pushSetSensorConfigState(s) 
#define pushSetSensorConfigInfo(v)	
#define pushSetSensorConfigInfoName(v,n) 
#define pushSetSensorConfigInfoMemComp(v)
#define pushSetSensorConfigInfoMemCompDouble(v)	

#define pushSetGroupInfo(v)
#define pushSetGroupInfoComp(v)
#define pushSetGroupInfoComp1(v)
#define pushSetGroupInfoCompUI32(v)	
#define pushSetGroupInfoCompName(m,v)
#define pushSetGroupInfoCompName1(m,v)
#define pushSetGroupInfoMem(v)
#define pushUpdateGroupInfo()

#endif	// ENABLE_PUSH


#endif // CTPUSH_PLUGIN_H
