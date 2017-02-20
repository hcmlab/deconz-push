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

//#define ENABLE_REACHABLE_TIMER

////////////////////////////////////////////////////////////////////////////////
// DEBUG symbol. Disable symbol to skip compiling debug log code
////////////////////////////////////////////////////////////////////////////////
#ifndef QT_NO_DEBUG 
#define DEBUG
#else // toremove
//#define DEBUG // toremove
#endif

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
    PushDevice () { SetIdentity ( 0, "Raspberry" ); };

    PushDevice ( uint64_t deviceMac, const char * deviceName );
    ~PushDevice();

    void SetIdentity ( uint64_t deviceMac, const char * deviceName );

    uint64_t        mac;
    std::string     name;
#ifdef ENABLE_REACHABLE_TIMER
	uint64_t		lastTS;
    int             lastMS;
#endif
    const Node *    node;
    int             type;
    bool            needsNodeUpdate;
    bool            needsRestUpdate;
    bool            available;
};


class PushGroup
{
public:
    PushGroup ( const char * groupName ) : on ( false ), colorX ( 0 ), colorY ( 0 ), hue ( 0 ), hueFloat ( 0 ), sat ( 0 ), level ( 0 ), 
                colorTemperature ( 0 )
                { name = groupName; };
    ~PushGroup();

    std::string     name;
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
    static bool                 enable_plugin;
    static bool                 enable_fhem_tunnel;
    static bool                 enable_push;

    bool                        fhemActive;
    bool                        acceptActive;
    bool                        pushActive;
    
    static bool                 fhem_node_update;

    static int                  fhem_port;
    static int                  push_port;

    // Construct / Destruct
    PushBridge ();
    ~PushBridge();

    void                        SetNode	            ( void * derestNode, void * denode );
    void                        SetNodeAvailable	( void * restNode, bool available );
    void                        SetNodeInfo			( void * restNode, int type, const char * reading, const QString & value );
    void                        SetNodeInfo			( void * restNode, int type, const char * reading, QString & value );
    void                        SetNodeInfo			( void * restNode, int type, const char * reading, uint32_t value );
    void                        SetNodeInfo			( void * restNode, QString & device, const char * reading, uint32_t value );
    void                        SetNodeInfoDouble   ( void * restNode, int type, const char * reading, double value );
    void                        SetNodeState		( void * restNode, int type, bool value, int pct );

	void                        SetGroupInfo		( QString & groupName, QString & id, const char * reading, uint32_t value );
	void                        SetGroupInfo		( const char * groupName, QString & id, const char * reading, uint32_t value );
	void                        SetGroupInfo		( const char * groupName, QString & id, const char * reading, float value );
	void                        SetGroupInfo		( QString & groupName, QString & id, const char * reading, const char * value );

    void                        EnqueueToFhem ( const char * cmd );
    
public Q_SLOTS:
    void apsdeDataIndication(const deCONZ::ApsDataIndication &ind);
    void apsdeDataConfirm(const deCONZ::ApsDataConfirm &conf);
    void nodeEvent              ( const deCONZ::NodeEvent & event );

#ifdef ENABLE_REACHABLE_TIMER
    void                        TimerAlive ();
#endif

private:
    int                         fhemSocket; // Telnet connection to fhem
    std::queue < char * >       fhemQueue;
    pthread_t                   fhemThread;
    pthread_mutex_t             fhemQueueLock;    
    pthread_cond_t              fhemSignal;
    bool                        fhemThreadRun;
    
    int                         configRequested;
    uint64_t                    lastConfigRequest;
    void                        RequestConfigFile ();
    void                        CheckConfigFile ();

    void                        EmptyFhemQueue ();
    void                        SignalFhemThread ();
    void                        SendToFhem ( const char * cmd );
    void				        FhemThread ();
    static void		*	        FhemThreadStarter ( void * arg ); 

    int                         acceptSocket;
    pthread_t                   acceptThread;
    bool                        acceptThreadRun;
    std::map < int, int >       pushLights;
    std::map < int, int >       pushSensors;
    std::map < int, int >       pushGroups;
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

#ifdef ENABLE_REACHABLE_TIMER
    QTimer  *                   alive_timer;
    uint64_t                    timerCurrent;
#endif
	pthread_mutex_t             groupLock;
	pthread_mutex_t             devicesLock;
    std::map < uint64_t, PushDevice * > devices;

#ifdef ENABLE_REACHABLE_TIMER
	std::map < uint64_t, PushDevice * > devicesTimer;
#endif
    std::map < std::string, PushGroup * > groups;

public:
    const char *                GetPushName		( uint64_t mac );
    PushDevice *                GetNodeDevice   ( void * voidNode, int type );
    PushDevice *                GetPushDevice	( uint64_t mac, bool update = true, bool checkCfg = true );
    bool                        AddPushDevice	( uint64_t mac, const char * name );
    bool                        AddPushGroup	( const char * dename, const char * fhemname );

    bool                        UpdatePushDevice ( uint64_t mac, const deCONZ::Node * node );
    void                        UpdatePushDevice ( PushDevice * device );

    PushGroup *                 GetPushGroup	( QString & groupName, bool checkCfg = true );

    void                        UpdatePushNode ( void * restNode, int type );
    void                        UpdatePushGroup ( QString & id );

    DEBUGL ( std::ofstream logfile; )
    DEBUGL ( std::ofstream logfile1; )
};

extern PushBridge pushBridge;

#define pushClassMethod()				virtual void UpdateToFhem ( PushDevice * device );
#define pushClassMethodSensor()		    pushClassMethod () void UpdateSensorFhem ();
#define pushClassExt()					pushClassMethod () PushDevice * m_pushDevice;
#define pushClassSensorExt()			pushClassExt () int m_pushId;
#define pushNodeExt()					pushClassExt () int m_pushType;

#define pushClassVarsInit()				m_pushDevice ( 0 ), 
#define pushSensorVarsInit()			pushClassVarsInit()  m_pushId ( -1 ),
#define pushNodeVarsInit()				pushClassVarsInit() m_pushType ( PUSH_TYPE_UNKNOWN ),

#define pushCode(code)					code

#define pushSetNodeState(s,l)				if ( pushBridge.enable_plugin ) pushBridge.SetNodeState ( this, 0, s, l )
#define pushSetNodeInfo(v)				if ( pushBridge.enable_plugin ) pushBridge.SetNodeInfo ( this, 0, #v, v )
#define pushSetNodeInfoName(v,n)		if ( pushBridge.enable_plugin ) pushBridge.SetNodeInfo ( this, 0, n, v )
#define pushSetNodeInfoComp(a,v)		if ( pushBridge.enable_plugin ) { if ( a != v ) { a = v; pushBridge.SetNodeInfo ( this, 0, #v, v ); } return; }
#define pushSetNodeInfoCompName(a,v,n)	if ( pushBridge.enable_plugin ) { if ( a != v ) { a = v; pushBridge.SetNodeInfo ( this, 0, n, v ); } return; }
#define pushSetNodeInfoCompName1(a,v,n)	if ( pushBridge.enable_plugin && a != v ) { pushBridge.SetNodeInfo ( this, 0, n, v ); }
#define pushSetNodeInfoMem(n)			if ( pushBridge.enable_plugin ) pushBridge.SetNodeInfo ( this, 0, #n, m_##n )
#define pushSetNodeInfoMemComp(v)		if ( pushBridge.enable_plugin ) { if ( m_##v != v ) { m_##v = v; pushBridge.SetNodeInfo ( this, 0, #v, v ); } return; }
#define pushSetNodeInfoMemComp1(v)		if ( pushBridge.enable_plugin && m_##v != v ) { pushBridge.SetNodeInfo ( this, 0, #v, v ); }
#define pushSetNodeInfoMemCompDouble(v)	if ( pushBridge.enable_plugin ) { if ( m_##v != v ) { m_##v = v; pushBridge.SetNodeInfoDouble ( this, 0, #v, v ); } return; }

#define pushSetNodeInfoCompNoDev(a,v)	if ( pushBridge.enable_plugin ) { if ( a != v ) { a = v; pushBridge.SetNodeInfo (  m_name, 0, #v, v ); } return; }

#define pushSetSensorStateInfo(v)		if ( pushBridge.enable_plugin ) pushBridge.SetNodeInfo ( this, 1, #v, v )
#define pushSetSensorStateInfoMem(n)    if ( pushBridge.enable_plugin ) pushBridge.SetNodeInfo ( this, 1, #n, m_##n )
#define pushSetSensorStateInfoMemComp(v) if ( pushBridge.enable_plugin ) { if ( m_##v != v ) { m_##v = v; pushBridge.SetNodeInfo ( this, 1, #v, v ); } return; }

#define pushSetSensorConfigState(s,l)     if ( pushBridge.enable_plugin ) pushBridge.SetNodeState ( this, 2, s, l )
#define pushSetSensorConfigInfo(v)		if ( pushBridge.enable_plugin ) pushBridge.SetNodeInfo ( this, 2, #v, v )
#define pushSetSensorConfigInfoName(v,n)  if ( pushBridge.enable_plugin ) pushBridge.SetNodeInfo ( this, 2, n, v )
#define pushSetSensorConfigInfoMemComp(v) if ( pushBridge.enable_plugin ) { if ( m_##v != v ) { m_##v = v; pushBridge.SetNodeInfo ( this, 2, #v, v ); } return; }
#define pushSetSensorConfigInfoMemCompDouble(v)	if ( pushBridge.enable_plugin ) { if ( m_##v != v ) { m_##v = v; pushBridge.SetNodeInfoDouble ( this, 2, #v, v ); } return; }

#define pushSetGroupInfo(v)				if ( pushBridge.enable_plugin ) pushBridge.SetGroupInfo ( m_name, m_id, #v, v )
#define pushSetGroupInfoComp(v)			if ( pushBridge.enable_plugin ) { if ( m_##v != v ) { m_##v = v; pushBridge.SetGroupInfo ( m_name, m_id, #v, v ); } return; }
#define pushSetGroupInfoComp1(v)		if ( pushBridge.enable_plugin ) { if ( m_##v != v ) { m_##v = v; pushBridge.SetGroupInfo ( m_name, m_id, #v, v ); } }
#define pushSetGroupInfoCompName(m,v)	if ( pushBridge.enable_plugin ) { if ( m_##m != v ) { m_##m = v; pushBridge.SetGroupInfo ( m_name, m_id, #m, v ); } return; }
#define pushSetGroupInfoCompName1(m,v)	if ( pushBridge.enable_plugin && m_##m != v )  { m_##m = v; pushBridge.SetGroupInfo ( m_name, m_id, #m, v ); }
#define pushSetGroupInfoMem(v)			if ( pushBridge.enable_plugin ) pushBridge.SetGroupInfo ( m_name, m_id, #v, m_##v )
#define pushUpdateGroupInfo()			if ( pushBridge.enable_plugin ) {\
                                            PushGroup * group = pushBridge.GetPushGroup ( m_name );\
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
#define pushSetGroupInfoCompName(m,v)
#define pushSetGroupInfoCompName1(m,v)
#define pushSetGroupInfoMem(v)
#define pushUpdateGroupInfo()

#endif	// ENABLE_PUSH


#endif // CTPUSH_PLUGIN_H
