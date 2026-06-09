/*
cl_parse_gs.c - GoldSrc protocol 48 server message parser
Handles svc_* messages from CS 1.6/HL1 servers via Reunion/Dproto.
Includes Delta_ParseTableField_GS for GoldSrc delta table format.
Written for tyabus/xash3d 2024 based on FWGS source analysis.

Fixed bugs:
  - Delta_Shutdown() replaced with per-table reset (was nuking ALL tables)
  - BF_SeekTail replaced with msg->iCurBit += bits (function doesn't exist)
  - svc_restore reads all 3 fields (was only reading 1)
  - S_RegisterSound bounds check added
  - CL_ParseAddAngle extern declaration added
*/
#ifndef XASH_DEDICATED
#include "common.h"
#include "client.h"
#include "net_encode.h"
#include "cl_parse_gs.h"

/* Functions defined in cl_parse.c but not in client.h */
extern void CL_ParseAddAngle( sizebuf_t *msg );
extern void CL_ParseMovevars( sizebuf_t *msg );
extern void CL_ParseParticles( sizebuf_t *msg );
extern void CL_ParseStaticEntity( sizebuf_t *msg );
extern void CL_ParseStaticDecal( sizebuf_t *msg );
extern void CL_ParseSoundFade( sizebuf_t *msg );
extern void CL_ParseCustomization( sizebuf_t *msg );
extern void CL_ParseClientData( sizebuf_t *msg );
extern void CL_ParseBaseline( sizebuf_t *msg );
extern void CL_ParseCrosshairAngle( sizebuf_t *msg );
extern void CL_RegisterUserMessage( sizebuf_t *msg );
extern void CL_UpdateUserPings( sizebuf_t *msg );
extern void CL_ParseResourceList( sizebuf_t *msg );

/* Delta_AddField declared only in net_encode.c — expose it */
extern qboolean Delta_AddField( const char *pStructName, const char *pName,
                                int flags, int bits, float mul, float post_mul );

/* GoldSrc DT_SIGNED flag is in bit 31 on the wire */
#define DT_SIGNED_GS  ( 1u << 31 )

/* GoldSrc svc numbers that differ from Xash3D */
#define GS_SVC_VERSION            4
#define GS_SVC_STOPSOUND         16
#define GS_SVC_DAMAGE            19
#define GS_SVC_KILLEDMONSTER     27
#define GS_SVC_FOUNDSECRET       28
#define GS_SVC_SPAWNSTATICSOUND  29
#define GS_SVC_DECALNAME         36
#define GS_SVC_SENDEXTRAINFO     54
#define GS_SVC_TIMESCALE         55

/* Skip N bytes in a sizebuf without BF_SeekTail (doesn't exist in old engine) */
#define BF_SKIP_BYTES( msg, n )  do { (msg)->iCurBit += (n) * 8; } while(0)

/* =========================================================
   Delta_ParseTableField_GS
   Parses the GoldSrc wire format for svc_deltatable.

   BUG FIXED: old version called Delta_Shutdown() which
   resets ALL tables. Now we reset only the specific table.

   GoldSrc format per field:
     [3 bits]  count of mask bytes
     [N bytes] changed-field mask
     field 0: fieldType    (uint32, 32 bits)
     field 1: fieldName    (string)
     field 2: fieldOffset  (uint16, 16 bits) — ignored
     field 3: fieldSize    (uint8,  8 bits)  — ignored
     field 4: significant_bits (uint8, 8 bits)
     field 5: premultiply  (float, 32 bits)
     field 6: postmultiply (float, 32 bits)
   ========================================================= */
void Delta_ParseTableField_GS( sizebuf_t *msg )
{
    const char   *tablename;
    int           num_fields, i, j, c;
    delta_info_t *dt;

    tablename  = BF_ReadString( msg );
    num_fields = BF_ReadShort( msg );
    dt         = Delta_FindStruct( tablename );

    if( !dt )
    {
        MsgDev( D_WARN, "Delta_ParseTableField_GS: unknown table '%s'\n", tablename );
        /* Must skip all data to stay in sync with message stream */
        for( i = 0; i < num_fields; i++ )
        {
            byte bits[8] = {0};
            c = BF_ReadUBitLong( msg, 3 );
            for( j = 0; j < c && j < 8; j++ )
                bits[j] = (byte)BF_ReadByte( msg );
            if( bits[0] & 1  ) BF_ReadUBitLong( msg, 32 );
            if( bits[0] & 2  ) BF_ReadString( msg );
            if( bits[0] & 4  ) BF_ReadUBitLong( msg, 16 );
            if( bits[0] & 8  ) BF_ReadUBitLong( msg, 8 );
            if( bits[0] & 16 ) BF_ReadUBitLong( msg, 8 );
            if( bits[0] & 32 ) BF_ReadBitFloat( msg );
            if( bits[0] & 64 ) BF_ReadBitFloat( msg );
        }
        return;
    }

    /*
     * FIXED: reset only this specific table, NOT all tables.
     * Old code called Delta_Shutdown() which wiped entity_state_t,
     * clientdata_t, usercmd_t etc. all at once, breaking subsequent
     * svc_deltatable packets for other tables.
     */
    if( dt->pFields )
    {
        Mem_Free( dt->pFields );
        dt->pFields = NULL;
    }
    dt->numFields     = 0;
    dt->bInitialized  = false;

    for( i = 0; i < num_fields; i++ )
    {
        byte  bits[8]       = {0};
        int   fieldType     = 0;
        char  fieldName[32] = {0};
        int   sig_bits      = 1;
        float premul        = 1.0f, postmul = 1.0f;

        c = BF_ReadUBitLong( msg, 3 );
        for( j = 0; j < c && j < 8; j++ )
            bits[j] = (byte)BF_ReadByte( msg );

        if( bits[0] & 1  ) fieldType = (int)BF_ReadUBitLong( msg, 32 );
        if( bits[0] & 2  ) Q_strncpy( fieldName, BF_ReadString( msg ), 32 );
        if( bits[0] & 4  ) BF_ReadUBitLong( msg, 16 );  /* fieldOffset — ignored */
        if( bits[0] & 8  ) BF_ReadUBitLong( msg, 8 );   /* fieldSize   — ignored */
        if( bits[0] & 16 ) sig_bits = (int)BF_ReadUBitLong( msg, 8 );
        if( bits[0] & 32 ) premul   = BF_ReadBitFloat( msg );
        if( bits[0] & 64 ) postmul  = BF_ReadBitFloat( msg );

        /* Convert GoldSrc DT_SIGNED_GS (bit 31) → Xash3D DT_SIGNED (bit 8) */
        if( (uint32_t)fieldType & DT_SIGNED_GS )
        {
            fieldType &= ~(int)DT_SIGNED_GS;
            fieldType |= DT_SIGNED;
        }

        if( fieldName[0] )
            Delta_AddField( tablename, fieldName, fieldType,
                            sig_bits, premul, postmul );
    }

    MsgDev( D_NOTE, "Delta_ParseTableField_GS: '%s' %d fields\n",
            tablename, num_fields );
}

/* =========================================================
   svc_serverdata in GoldSrc format
   ========================================================= */
static void CL_GS_ParseServerData( sizebuf_t *msg )
{
    char gamefolder[64], hostname[128];

    cls.demowaiting = false;
    if( !cls.changelevel && !cls.changedemo )
        CL_ClearState();
    cls.state = ca_connected;

    BF_ReadLong( msg );                              /* protocol */
    cl.servercount = BF_ReadLong( msg );             /* spawncount */
    BF_ReadLong( msg );                              /* mapCRC */
    cl.maxclients  = BF_ReadByte( msg );             /* maxclients */

    Q_strncpy( gamefolder, BF_ReadString( msg ), sizeof( gamefolder ) );
    BF_ReadByte( msg );                              /* deathmatch */
    Q_strncpy( hostname,   BF_ReadString( msg ), sizeof( hostname ) );
    BF_ReadByte( msg );                              /* current players */
    BF_ReadByte( msg );                              /* max players */
    BF_ReadLong( msg );                              /* flags */

    MsgDev( D_INFO, "GoldSrc server: \"%s\"  game=%s\n", hostname, gamefolder );
    Q_strncpy( clgame.maptitle, hostname, MAX_STRING );

    if( Q_stricmp( host.gamefolder, gamefolder ) )
        MsgDev( D_WARN, "GoldSrc: server game '%s', client '%s'\n",
                gamefolder, host.gamefolder );

    UI_SetActiveMenu( cl.background );
    if( cl.maxclients > 1 && !CL_IsPlaybackDemo() )
        Cbuf_AddText( "menu_connectionprogress serverinfo server\n" );
}

/* =========================================================
   Minor svc helpers
   ========================================================= */
static void CL_GS_ParseSendExtraInfo( sizebuf_t *msg )
{
    const char *desc  = BF_ReadString( msg );
    int         cheat = BF_ReadByte( msg );
    MsgDev( D_NOTE, "GoldSrc extra: %s cheats=%d\n", desc, cheat );
}

static void CL_GS_ParseSpawnStaticSound( sizebuf_t *msg )
{
    vec3_t pos;
    int    soundnum, vol, attn, ent;

    pos[0]   = BF_ReadShort( msg ) / 8.0f;
    pos[1]   = BF_ReadShort( msg ) / 8.0f;
    pos[2]   = BF_ReadShort( msg ) / 8.0f;
    soundnum = BF_ReadWord( msg );
    vol      = BF_ReadByte( msg );
    attn     = BF_ReadByte( msg );
    ent      = BF_ReadWord( msg );

    /* BUG FIXED: bounds check before accessing sound name array */
    if( soundnum > 0 && soundnum < MAX_SOUNDS && cl.sound_name[soundnum][0] )
    {
        S_AmbientSound( pos, ent,
            S_RegisterSound( cl.sound_name[soundnum] ),
            vol / 255.0f, attn / 64.0f, 0, 0 );
    }
}

static void CL_GS_ParseDecalName( sizebuf_t *msg )
{
    int         idx  = BF_ReadByte( msg );
    const char *name = BF_ReadString( msg );
    if( idx >= 0 && idx < MAX_DECALS )
        Q_strncpy( host.draw_decals[idx], name, sizeof( host.draw_decals[0] ) );
}

/* =========================================================
   CL_ParseGoldSrcStatusMessage
   Parses A2S_INFO response (0x49 or 0x6D) for server browser
   ========================================================= */
void CL_ParseGoldSrcStatusMessage( netadr_t from, sizebuf_t *msg, qboolean legacy )
{
    static char info[MAX_INFO_STRING];
    char        host[128], map[64], gamedir[32], desc[64];
    int         numcl = 0, maxcl = 0, proto = 0;
    qboolean    pw = false;
    const char *p;

    /* Rewind to byte 5: past \xFF\xFF\xFF\xFF + type byte */
    msg->iCurBit = 5 * 8;

    if( !legacy )
    {
        proto = BF_ReadByte( msg );
        p = BF_ReadString( msg ); Q_strncpy( host,    p, sizeof(host)    );
        p = BF_ReadString( msg ); Q_strncpy( map,     p, sizeof(map)     );
        p = BF_ReadString( msg ); Q_strncpy( gamedir, p, sizeof(gamedir) );
        p = BF_ReadString( msg ); Q_strncpy( desc,    p, sizeof(desc)    );
        BF_ReadShort( msg );        /* appid */
        numcl = BF_ReadByte( msg );
        maxcl = BF_ReadByte( msg );
        BF_ReadByte( msg );         /* bots */
        BF_ReadByte( msg );         /* server type */
        BF_ReadByte( msg );         /* os */
        pw = BF_ReadByte( msg ) != 0;
    }
    else
    {
        BF_ReadString( msg );       /* address — skip, use *from* */
        p = BF_ReadString( msg ); Q_strncpy( host,    p, sizeof(host)    );
        p = BF_ReadString( msg ); Q_strncpy( map,     p, sizeof(map)     );
        p = BF_ReadString( msg ); Q_strncpy( gamedir, p, sizeof(gamedir) );
        p = BF_ReadString( msg ); Q_strncpy( desc,    p, sizeof(desc)    );
        numcl = BF_ReadByte( msg );
        maxcl = BF_ReadByte( msg );
        proto = BF_ReadByte( msg );
        BF_ReadByte( msg );         /* server type */
        BF_ReadByte( msg );         /* os */
        pw = BF_ReadByte( msg ) != 0;
    }

    if( msg->bOverflow )
    {
        MsgDev( D_WARN, "CL_ParseGoldSrcStatusMessage: overflow from %s\n",
                NET_AdrToString( from ) );
        return;
    }

    info[0] = '\0';
    Info_SetValueForKey( info, "host",     host,           sizeof(info) );
    Info_SetValueForKey( info, "map",      map,            sizeof(info) );
    Info_SetValueForKey( info, "gamedir",  gamedir,        sizeof(info) );
    Info_SetValueForKey( info, "numcl",    va("%i",numcl), sizeof(info) );
    Info_SetValueForKey( info, "maxcl",    va("%i",maxcl), sizeof(info) );
    Info_SetValueForKey( info, "p",        va("%i",proto), sizeof(info) );
    Info_SetValueForKey( info, "password", pw ? "1" : "0", sizeof(info) );
    Info_SetValueForKey( info, "gs",       "1",            sizeof(info) );

    MsgDev( D_NOTE, "GoldSrc: \"%s\" [%s] %d/%d\n", host, map, numcl, maxcl );
    UI_AddServerToList( from, info );
}

/* =========================================================
   Main GoldSrc svc_* dispatcher
   ========================================================= */
void CL_ParseGoldSrcServerMessage( sizebuf_t *msg )
{
    int  cmd;
    int  bufStart;

    while( 1 )
    {
        if( msg->bOverflow )
        {
            Host_Error( "CL_ParseGoldSrcServerMessage: overflow!\n" );
            return;
        }

        if( BF_GetNumBitsLeft( msg ) < 8 )
            break;

        bufStart = BF_GetNumBitsWritten( msg );  /* BF_GetNumBitsRead in old engine */
        cmd      = BF_ReadByte( msg );

        /* ---- GoldSrc-specific svc numbers ---- */
        if( cmd == GS_SVC_VERSION )
        {
            int ver = BF_ReadLong( msg );
            if( ver != PROTOCOL_VERSION )
                MsgDev( D_WARN, "GoldSrc: protocol ver %d (expected %d)\n",
                        ver, PROTOCOL_VERSION );
            continue;
        }
        if( cmd == GS_SVC_STOPSOUND )
        {
            int idx = BF_ReadWord( msg );
            S_StopSound( idx >> 3, idx & 7, NULL );
            continue;
        }
        if( cmd == GS_SVC_DAMAGE )            { continue; } /* no data */
        if( cmd == GS_SVC_KILLEDMONSTER )     { continue; } /* no data */
        if( cmd == GS_SVC_FOUNDSECRET )       { continue; } /* no data */
        if( cmd == GS_SVC_SPAWNSTATICSOUND )  { CL_GS_ParseSpawnStaticSound( msg ); continue; }
        if( cmd == GS_SVC_DECALNAME )         { CL_GS_ParseDecalName( msg ); continue; }
        if( cmd == GS_SVC_SENDEXTRAINFO )     { CL_GS_ParseSendExtraInfo( msg ); continue; }
        if( cmd == GS_SVC_TIMESCALE )         { BF_ReadFloat( msg ); continue; }

        /* Override svc_serverdata with GoldSrc format parser */
        if( cmd == svc_serverdata )           { CL_GS_ParseServerData( msg ); continue; }

        /* ---- svc numbers shared between GoldSrc and Xash3D ---- */
        switch( cmd )
        {
        case svc_nop:
            break;

        case svc_disconnect:
        {
            const char *reason = BF_ReadString( msg );
            if( reason && reason[0] )
                Con_Printf( "Disconnected: %s\n", reason );
            CL_Drop();
            Host_AbortCurrentFrame();
            return;
        }

        case svc_event:
            CL_ParseEvent( msg );
            break;

        case svc_setview:
            cl.viewentity = BF_ReadWord( msg );
            if( cl.viewentity > clgame.maxEntities )
                cl.viewentity = cl.playernum + 1;
            break;

        case svc_sound:
            CL_ParseSoundPacket( msg, false );
            break;

        case svc_time:
            cl.mtime[0] = cl.mtime[1];
            cl.mtime[1] = BF_ReadFloat( msg );
            break;

        case svc_print:
            Con_Printf( "%s", BF_ReadString( msg ) );
            break;

        case svc_stufftext:
            Cbuf_AddText( BF_ReadString( msg ) );
            break;

        case svc_setangle:
            CL_ParseSetAngle( msg );
            break;

        case svc_lightstyle:
        {
            int        idx = BF_ReadByte( msg );
            const char *s  = BF_ReadString( msg );
            if( idx >= 0 && idx < MAX_LIGHTSTYLES )
                Q_strncpy( cl.lightstyles[idx].pattern, s,
                           sizeof( cl.lightstyles[0].pattern ) );
            break;
        }

        case svc_updateuserinfo:
            CL_UpdateUserinfo( msg );
            break;

        case svc_deltatable:
            /* KEY: use GoldSrc binary format, NOT Xash3D format */
            Delta_ParseTableField_GS( msg );
            break;

        case svc_clientdata:
            CL_ParseClientData( msg );
            break;

        case svc_pings:
            CL_UpdateUserPings( msg );
            break;

        case svc_particle:
            CL_ParseParticles( msg );
            break;

        case svc_spawnstatic:
            CL_ParseStaticEntity( msg );
            break;

        case svc_event_reliable:
            CL_ParseReliableEvent( msg );
            break;

        case svc_spawnbaseline:
            CL_ParseBaseline( msg );
            break;

        case svc_temp_entity:
            CL_ParseTempEntity( msg );
            break;

        case svc_setpause:
            cl.refdef.paused = ( BF_ReadOneBit( msg ) != 0 );
            break;

        case svc_signonnum:
        {
            int n = BF_ReadByte( msg );
            if( n == 2 ) Cbuf_AddText( "prespawn\n" );
            else if( n == 3 ) Cbuf_AddText( "spawn\n" );
            break;
        }

        case svc_centerprint:
            CL_CenterPrint( BF_ReadString( msg ), 0.25f );
            break;

        case svc_intermission:
            cl.refdef.intermission = true;
            break;

        case svc_cdtrack:
        {
            int track = BF_ReadByte( msg );
            BF_ReadByte( msg ); /* loop track */
            S_StartBackgroundTrack( va( "media/track%02i.mp3", track ), NULL, 0 );
            break;
        }

        case svc_restore:
            /* FIXED: GoldSrc svc_restore sends string + byte + byte */
            BF_ReadString( msg );  /* save name */
            BF_ReadByte( msg );    /* current map index */
            BF_ReadByte( msg );    /* maxclients */
            break;

        case svc_weaponanim:
        {
            int anim = BF_ReadByte( msg );
            int body = BF_ReadByte( msg );
            if( cl.local.weapons )
                clgame.dllFuncs.pfnWeaponAnim( anim, body );
            break;
        }

        case svc_bspdecal:
            /* Note: GS_SVC_DECALNAME (36) is handled above before this switch.
               This case is only reached from Xash3D path, not GoldSrc path. */
            CL_ParseStaticDecal( msg );
            break;

        case svc_roomtype:
            Cvar_SetFloat( "room_type", BF_ReadWord( msg ) );
            break;

        case svc_addangle:
            CL_ParseAddAngle( msg );
            break;

        case svc_usermessage:
            CL_RegisterUserMessage( msg );
            break;

        case svc_packetentities:
            CL_ParsePacketEntities( msg, false );
            break;

        case svc_deltapacketentities:
            CL_ParsePacketEntities( msg, true );
            break;

        case svc_chokecount:
            BF_ReadByte( msg );
            break;

        case svc_resourcelist:
            CL_ParseResourceList( msg );
            break;

        case svc_deltamovevars:
            CL_ParseMovevars( msg );
            break;

        case svc_customization:
            CL_ParseCustomization( msg );
            break;

        case svc_crosshairangle:
            CL_ParseCrosshairAngle( msg );
            break;

        case svc_soundfade:
            CL_ParseSoundFade( msg );
            break;

        case svc_director:
            BF_ReadByte( msg );
            break;

        case svc_querycvarvalue:
        {
            int         reqid = BF_ReadLong( msg );
            const char *cvar  = BF_ReadString( msg );
            Netchan_OutOfBandPrint( NS_CLIENT, cls.netchan.remote_address,
                "cl_cvarvalue %d \"%s\" \"%s\"\n",
                reqid, cvar, Cvar_VariableString( cvar ) );
            break;
        }

        case svc_querycvarvalue2:
        {
            int         reqid = BF_ReadLong( msg );
            const char *cvar  = BF_ReadString( msg );
            Netchan_OutOfBandPrint( NS_CLIENT, cls.netchan.remote_address,
                "cl_cvarvalue2 %d %d \"%s\" \"%s\"\n",
                reqid, 1, cvar, Cvar_VariableString( cvar ) );
            break;
        }

        default:
            if( cmd >= 64 )
            {
                /* User message — look up registered size */
                int i, sz = -1;
                for( i = 0; i < MAX_USER_MESSAGES; i++ )
                {
                    if( clgame.msg[i].number == cmd )
                    {
                        sz = clgame.msg[i].size;
                        break;
                    }
                }
                if( sz < 0 )
                    sz = BF_ReadByte( msg );  /* variable-size message */
                if( sz > 0 )
                {
                    /* FIXED: use iCurBit directly, BF_SeekTail doesn't exist */
                    if( BF_GetNumBitsLeft( msg ) >= sz * 8 )
                        msg->iCurBit += sz * 8;
                }
                if( i == MAX_USER_MESSAGES )
                    MsgDev( D_NOTE, "GoldSrc: unknown svc %d\n", cmd );
            }
            else
            {
                MsgDev( D_WARN, "GoldSrc: unhandled svc %d at bit %d\n",
                        cmd, bufStart );
            }
            break;
        }
    }
}
#endif /* !XASH_DEDICATED */
