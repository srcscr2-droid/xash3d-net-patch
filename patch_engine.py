#!/usr/bin/env python3
"""
patch_engine.py - 100% network patch for tyabus/xash3d
Based on reading ACTUAL source code of both engines.
"""
import os, sys, re

def read(p):
    with open(p,"r",encoding="utf-8",errors="replace") as f: return f.read()
def write(p,t):
    with open(p,"w",encoding="utf-8") as f: f.write(t)
    print(f"  patched  {os.path.relpath(p)}")
def skip(n): print(f"  skip     {n}")
def warn(m): print(f"  WARN     {m}")

def patch_netadr_h(root):
    path = os.path.join(root,"common","netadr.h")
    src  = read(path)
    if "NA_IP6" in src: return skip("common/netadr.h")
    OLD = "typedef enum {NA_LOOPBACK = 1, NA_BROADCAST, NA_IP, NA_IPX, NA_BROADCAST_IPX} netadrtype_t;"
    NEW = "typedef enum {NA_LOOPBACK = 1, NA_BROADCAST, NA_IP, NA_IPX, NA_BROADCAST_IPX, NA_IP6 = 6} netadrtype_t;"
    if OLD not in src: return warn("netadr.h: enum not found")
    OLD2 = "\tunsigned char\tipx[10];\n\tunsigned short\tport;\n} netadr_t;"
    NEW2 = "\tunsigned char\tipx[10];\n\tunsigned char\tip6[16];\n\tunsigned short\tport;\n} netadr_t;"
    src = src.replace(OLD, NEW, 1).replace(OLD2, NEW2, 1)
    write(path, src)

def patch_common_h(root):
    path = os.path.join(root,"engine","common","common.h")
    src  = read(path)
    if "NET_MasterQuery" in src: return skip("common.h")
    OLD = "qboolean NET_IsFromMasters( netadr_t adr );"
    NEW = OLD+"\nqboolean NET_MasterQuery( uint32_t key, qboolean nat, const char *filter );"
    if OLD not in src: return warn("common.h: NET_IsFromMasters not found")
    write(path, src.replace(OLD,NEW,1))

def patch_client_h(root):
    path = os.path.join(root,"engine","client","client.h")
    src  = read(path)
    if "internetservers_key" in src: return skip("client.h")
    OLD = "\tqboolean internetservers_pending;\t// internetservers is waiting for dns request"
    NEW = (OLD+"\n"
           "\tuint32_t internetservers_key;\t\t// key sent with last master query\n"
           "\tqboolean internetservers_nat;\t\t// NAT mode flag\n"
           "\tconvar_t *cl_goldsrc;\t\t\t// use GoldSrc parser for CS 1.6 servers")
    if OLD not in src: return warn("client.h: anchor not found")
    write(path, src.replace(OLD,NEW,1))

def patch_network_c(root):
    path = os.path.join(root,"engine","common","network.c")
    src  = read(path)
    if "NA_IP6" in src: return skip("network.c")

    # Add IPv6 headers if not present
    if "netinet6" not in src and "AF_INET6" not in src:
        # Find platform-specific include block and add after it
        for anchor in ["#include <sys/socket.h>", "#include <netinet/in.h>"]:
            if anchor in src:
                src = src.replace(anchor,
                    anchor + "\n#include <netinet/in.h>\n#include <arpa/inet.h>\n#include <net/if.h>",
                    1)
                break

    if "ip6_sockets" not in src:
        src = src.replace("static int\t\tip_sockets[NS_COUNT];",
            "static int\t\tip_sockets[NS_COUNT];\nstatic int\t\tip6_sockets[NS_COUNT];",1)

    # NET_StringToAdr: add IPv6 parsing at top
    IPV6 = (
        "\t// IPv6: [2001:db8::1]:port\n"
        "\tif( string[0] == '[\' )\n"
        "\t{\n"
        "\t\tchar ip6buf[64]; int port6 = PORT_SERVER;\n"
        "\t\tconst char *close = strchr( string, ']' );\n"
        "\t\tif( close ) {\n"
        "\t\t\tint iplen = (int)(close - string) - 1;\n"
        "\t\t\tif( iplen > 0 && iplen < 64 ) {\n"
        "\t\t\t\tmemcpy( ip6buf, string+1, iplen ); ip6buf[iplen] = 0;\n"
        "\t\t\t\tif( close[1] == ':' ) port6 = Q_atoi( close+2 );\n"
        "\t\t\t\tif( inet_pton( AF_INET6, ip6buf, adr->ip6 ) == 1 ) {\n"
        "\t\t\t\t\tadr->type = NA_IP6;\n"
        "\t\t\t\t\tadr->port = BF_BigShort((short)port6);\n"
        "\t\t\t\t\treturn true;\n"
        "\t\t\t\t}\n"
        "\t\t\t}\n"
        "\t\t}\n"
        "\t}\n"
    )
    STR2ADR = ("qboolean NET_StringToAdr( const char *string, netadr_t *adr )\n"
               "{\n"
               "\tstruct sockaddr s;\n"
               "\n"
               "\tQ_memset( adr, 0, sizeof( netadr_t ));\n"
               "\tif( !Q_stricmp( string, \"localhost\" ) || !Q_stricmp( string, \"loopback\" ) )\n")
    if "[ipv6]" not in src and STR2ADR in src:
        src = src.replace(STR2ADR,
            STR2ADR.replace("\tQ_memset( adr, 0, sizeof( netadr_t ));\n"
                           "\tif( !Q_stricmp",
                            "\tQ_memset( adr, 0, sizeof( netadr_t ));\n"
                           + IPV6 +
                            "\tif( !Q_stricmp"), 1)

    # NET_AdrToString: add NA_IP6
    ADR2STR = "\tif( adr.type == NA_LOOPBACK )"
    NA6 = ("\tif( adr.type == NA_IP6 )\n"
           "\t{\n"
           "\t\tchar ipstr[INET6_ADDRSTRLEN]; struct in6_addr a6;\n"
           "\t\tmemcpy( &a6, adr.ip6, 16 );\n"
           "\t\tinet_ntop( AF_INET6, &a6, ipstr, sizeof(ipstr) );\n"
           "\t\tQ_snprintf( s, sizeof(s), \"[%s]:%i\", ipstr, ntohs(adr.port) );\n"
           "\t\treturn s;\n"
           "\t}\n"
           "\tif( adr.type == NA_LOOPBACK )")
    if "NA_IP6" not in src and ADR2STR in src:
        src = src.replace(ADR2STR, NA6, 1)

    # NET_SendPacket: route IPv6
    SENDP = "\tret = pSendTo( net_socket, data, length, 0, &addr, sizeof( addr ));"
    SENDP_NEW = ("\tif( to.type == NA_IP6 && ip6_sockets[sock] > 0 )\n"
                 "\t{\n"
                 "\t\tstruct sockaddr_in6 a6; memset(&a6,0,sizeof(a6));\n"
                 "\t\ta6.sin6_family=AF_INET6; a6.sin6_port=to.port;\n"
                 "\t\tmemcpy(&a6.sin6_addr,to.ip6,16);\n"
                 "\t\tpSendTo(ip6_sockets[sock],data,length,0,(struct sockaddr*)&a6,sizeof(a6));\n"
                 "\t\treturn;\n"
                 "\t}\n"
                 + SENDP)
    if "ip6_sockets" in src and "to.type == NA_IP6" not in src and SENDP in src:
        src = src.replace(SENDP, SENDP_NEW, 1)


    # Close IPv6 sockets on NET_Config(false)
    OLD_NET_CLOSE = "\t\t\tip_sockets[NS_SERVER] = 0;"
    NEW_NET_CLOSE = ("\t\t\tip_sockets[NS_SERVER] = 0;\n"
                     "\t\t\tif(ip6_sockets[NS_SERVER]>0){close(ip6_sockets[NS_SERVER]);ip6_sockets[NS_SERVER]=0;}")
    if "ip6_sockets[NS_SERVER] = 0" not in src and OLD_NET_CLOSE in src:
        src = src.replace(OLD_NET_CLOSE, NEW_NET_CLOSE, 1)

    write(path, src)

def patch_cl_main(root):
    path = os.path.join(root,"engine","client","cl_main.c")
    src  = read(path)

    # includes
    for inc in ['"cl_serverlist.h"', '"cl_parse_gs.h"']:
        if inc not in src:
            src = src.replace('#include "library.h"',
                              '#include "library.h"\n#include '+inc, 1)

    # CL_InternetServers_f
    if "NET_MasterQuery" not in src:
        src = re.sub(
            r'void CL_InternetServers_f\s*\(\s*void\s*\)\s*\{.*?\n\}',
            ('void CL_InternetServers_f( void )\n'
             '{\n'
             '\tNET_Config( true, true );\n'
             '\tcls.internetservers_key  = COM_RandomLong( 1, 0x7FFFFFFF );\n'
             '\tcls.internetservers_nat  = (qboolean)cl_nat->integer;\n'
             '\tcls.internetservers_wait = NET_MasterQuery(\n'
             '\t\tcls.internetservers_key, cls.internetservers_nat, GI->gamefolder );\n'
             '\tcls.internetservers_pending = true;\n'
             '}'),
            src, count=1, flags=re.DOTALL|re.MULTILINE)

    # "f" handler with key check + GoldSrc A2S
    OLD_F = ("\t\t// serverlist got from masterserver\n"
             "\t\twhile( !msg->bOverflow )\n"
             "\t\t{\n"
             "\t\t\tservadr.type = NA_IP;\n"
             "\t\t\t// 4 bytes for IP\n"
             "\t\t\tBF_ReadBytes( msg, servadr.ip.u8, sizeof( servadr.ip ));\n"
             "\t\t\t// 2 bytes for Port\n"
             "\t\t\tservadr.port = BF_ReadShort( msg );\n"
             "\n"
             "\t\t\tif( !servadr.port )\n"
             "\t\t\t\tbreak;\n"
             "\n"
             "\t\t\tMsgDev( D_INFO, \"Found server: %s\\n\", NET_AdrToString( servadr ));\n"
             "\n"
             "\t\t\tNET_Config( true, false ); // allow remote\n"
             "\n"
             "\t\t\tNetchan_OutOfBandPrint( NS_CLIENT, servadr, \"info %i\", PROTOCOL_VERSION );\n"
             "\t\t}")
    NEW_F = ("\t\t// serverlist got from masterserver\n"
             "\t\t// Check for Xash3D key header (0x7f)\n"
             "\t\tif( BF_GetNumBytesLeft( msg ) > 6 )\n"
             "\t\t{\n"
             "\t\t\tbyte *raw = msg->pData + (msg->iCurBit >> 3);\n"
             "\t\t\tif( raw[0] == 0x7F )\n"
             "\t\t\t{\n"
             "\t\t\t\tuint32_t rkey;\n"
             "\t\t\t\tBF_ReadByte( msg );\n"
             "\t\t\t\trkey  = (uint32_t)BF_ReadByte( msg );\n"
             "\t\t\t\trkey |= (uint32_t)BF_ReadByte( msg ) << 8;\n"
             "\t\t\t\trkey |= (uint32_t)BF_ReadByte( msg ) << 16;\n"
             "\t\t\t\trkey |= (uint32_t)BF_ReadByte( msg ) << 24;\n"
             "\t\t\t\tBF_ReadByte( msg );\n"
             "\t\t\t\tif( cls.internetservers_key && rkey && rkey != cls.internetservers_key )\n"
             "\t\t\t\t{ MsgDev(D_WARN,\"master key mismatch\\n\"); return; }\n"
             "\t\t\t}\n"
             "\t\t}\n"
             "\t\twhile( !msg->bOverflow )\n"
             "\t\t{\n"
             "\t\t\tservadr.type = NA_IP;\n"
             "\t\t\tBF_ReadBytes( msg, servadr.ip.u8, sizeof( servadr.ip ));\n"
             "\t\t\tservadr.port = BF_ReadShort( msg );\n"
             "\t\t\tif( !servadr.port ) break;\n"
             "\t\t\tMsgDev( D_INFO, \"Found server: %s\\n\", NET_AdrToString( servadr ));\n"
             "\t\t\tNET_Config( true, false );\n"
             "\t\t\tNetchan_OutOfBandPrint( NS_CLIENT, servadr, \"info %i\", PROTOCOL_VERSION );\n"
             "\t\t\t{ static const char a2s[] = \"TSource Engine Query\";\n"
             "\t\t\t  Netchan_OutOfBand( NS_CLIENT, servadr, sizeof(a2s), (byte*)a2s ); }\n"
             "\t\t}")
    if "GoldSrc A2S" not in src and OLD_F in src:
        src = src.replace(OLD_F, NEW_F, 1)


    # OOB handlers for 0x49 and 0x6D
    OLD_OOB = ("\telse if( !Q_strcmp( c, \"info\" ))\n"
               "\t{\n"
               "\t\t// server responding to a status broadcast\n"
               "\t\tCL_ParseStatusMessage( from, msg );\n"
               "\t}")
    NEW_OOB = (OLD_OOB+"\n"
               "\telse if( c[0] == 'I' )\n"
               "\t{\n\t\tCL_ParseGoldSrcStatusMessage( from, msg, false );\n\t}\n"
               "\telse if( c[0] == 'm' )\n"
               "\t{\n\t\tCL_ParseGoldSrcStatusMessage( from, msg, true );\n\t}")
    if "c[0] == 'I'" not in src and OLD_OOB in src:
        src = src.replace(OLD_OOB, NEW_OOB, 1)

    # Reunion/Dproto: add prot=2 to userinfo
    OLD_UA = "Info_SetValueForKey( useragent, \"i\", ID_GetMD5(), sizeof( useragent ) );"
    NEW_UA = (OLD_UA+"\n\n"
              "\t// GoldSrc compat: set prot=2 for Reunion/Dproto\n"
              "\tif( adr.type != NA_LOOPBACK && cls.cl_goldsrc && cls.cl_goldsrc->integer )\n"
              "\t{\n"
              "\t\tchar uinfo[MAX_INFO_STRING];\n"
              "\t\tQ_strncpy( uinfo, Cvar_Userinfo(), sizeof(uinfo) );\n"
              "\t\tInfo_SetValueForKey( uinfo, \"prot\",   \"2\",  sizeof(uinfo) );\n"
              "\t\tInfo_SetValueForKey( uinfo, \"unique\", \"-1\", sizeof(uinfo) );\n"
              "\t\tNetchan_OutOfBandPrint( NS_CLIENT, adr, \"connect %i %i %i \\\"%s\\\" %d %s\\n\",\n"
              "\t\t\tPROTOCOL_VERSION, port, cls.challenge, uinfo, extensions, useragent );\n"
              "\t\treturn;\n"
              "\t}")
    if "prot" not in src and OLD_UA in src:
        src = src.replace(OLD_UA, NEW_UA, 1)

    # Route to GoldSrc parser — patch cl_parse.c (not cl_main.c)
    parse_path = os.path.join(root,"engine","client","cl_parse.c")
    if os.path.exists(parse_path):
        psrc = read(parse_path)
        if "CL_ParseGoldSrcServerMessage" not in psrc:
            if '"cl_parse_gs.h"' not in psrc:
                psrc = psrc.replace('#include "common.h"',
                    '#include "common.h"\n#include "cl_parse_gs.h"', 1)
            OLD_PARSE = "void CL_ParseServerMessage( sizebuf_t *msg )\n{"
            NEW_PARSE = ("void CL_ParseServerMessage( sizebuf_t *msg )\n"
                         "{\n"
                         "\tif( cls.cl_goldsrc && cls.cl_goldsrc->integer )\n"
                         "\t{ CL_ParseGoldSrcServerMessage( msg ); return; }\n")
            if OLD_PARSE in psrc:
                psrc = psrc.replace(OLD_PARSE, NEW_PARSE, 1)
                write(parse_path, psrc)

    # cl_goldsrc cvar registration
    OLD_NAT_CVAR = "cl_nat = Cvar_Get( \"cl_nat\", \"0\", 0, \"Show servers running under nat\" );"
    NEW_NAT_CVAR = (OLD_NAT_CVAR+"\n\t"
                   "cls.cl_goldsrc = Cvar_Get( \"cl_goldsrc\", \"0\", CVAR_ARCHIVE, \"GoldSrc/CS 1.6 server parser\" );")
    if "cl_goldsrc" not in src and OLD_NAT_CVAR in src:
        src = src.replace(OLD_NAT_CVAR, NEW_NAT_CVAR, 1)

    # History
    OLD_CONN = ("cls.state = ca_connecting;\n"
                "\tQ_strncpy( cls.servername, server, sizeof( cls.servername ));\n"
                "\tcls.connect_time = MAX_HEARTBEAT;")
    if "SL_History_AddEntry" not in src and OLD_CONN in src:
        src = src.replace(OLD_CONN, OLD_CONN+"\n\tSL_History_AddEntry( server );", 1)

    # SL_Init
    OLD_CMD = "\tCmd_AddCommand (\"internetservers\", CL_InternetServers_f, \"collect info about internet servers\" );"
    if "SL_Init" not in src and OLD_CMD in src:
        src = src.replace(OLD_CMD, OLD_CMD+"\n\tSL_Init();", 1)

    # SL_Shutdown
    OLD_SHUT = "\tS_Shutdown ();\n\tR_Shutdown ();"
    if "SL_Shutdown" not in src and OLD_SHUT in src:
        src = src.replace(OLD_SHUT, "\tSL_Shutdown();\n"+OLD_SHUT, 1)

    write(path, src)

def patch_android_mk(root):
    path = os.path.join(root,"engine","Android.mk")
    if not os.path.exists(path): return warn("Android.mk not found")
    src = read(path)
    if "cl_serverlist.c" in src: return skip("Android.mk")
    OLD = "           client/cl_main.c \\"
    NEW = ("           client/cl_main.c \\\n"
           "           client/cl_serverlist.c \\\n"
           "           client/cl_parse_gs.c \\")
    if OLD not in src: return warn("Android.mk: cl_main.c not found")
    write(path, src.replace(OLD,NEW,1))

def run_tests(root):
    print("\n=== SELF-TEST ===")
    ok = True
    def chk(f,needle,label):
        nonlocal ok
        p=os.path.join(root,f)
        if not os.path.exists(p): print(f"  MISSING  {f}"); ok=False; return
        if needle in read(p): print(f"  OK       {label}")
        else: print(f"  FAIL     {label}"); ok=False
    chk("common/netadr.h","NA_IP6 = 6","IPv6 type")
    chk("common/netadr.h","ip6[16]","ip6 field")
    chk("engine/common/common.h","NET_MasterQuery","NET_MasterQuery decl")
    chk("engine/client/client.h","internetservers_key","key field")
    chk("engine/client/client.h","cl_goldsrc","cl_goldsrc cvar field")
    chk("engine/client/cl_main.c","cl_parse_gs.h","cl_parse_gs include")
    chk("engine/client/cl_main.c","cl_serverlist.h","cl_serverlist include")
    chk("engine/client/cl_main.c","NET_MasterQuery","NET_MasterQuery usage")
    chk("engine/client/cl_main.c","0x7F","key validation")
    chk("engine/client/cl_main.c","TSource Engine Query","GoldSrc A2S query")
    chk("engine/client/cl_main.c","CL_ParseGoldSrcStatus","GoldSrc status parser")
    chk("engine/client/cl_main.c","c[0] == 'I'","0x49 OOB handler")
    chk("engine/client/cl_main.c","c[0] == 'm'","0x6D OOB handler")
    chk("engine/client/cl_parse.c","CL_ParseGoldSrcServerMessage","GoldSrc server msg route")
    chk("engine/client/cl_main.c","cl_goldsrc","cl_goldsrc cvar init")
    chk("engine/client/cl_main.c","SL_History_AddEntry","history on connect")
    chk("engine/client/cl_main.c","SL_Init","SL_Init call")
    chk("engine/client/cl_main.c","SL_Shutdown","SL_Shutdown call")
    chk("engine/Android.mk","cl_serverlist.c","cl_serverlist in build")
    chk("engine/Android.mk","cl_parse_gs.c","cl_parse_gs in build")
    chk("engine/client/cl_serverlist.c","SL_Init","cl_serverlist.c present")
    chk("engine/client/cl_parse_gs.c","CL_ParseGoldSrcServerMessage","cl_parse_gs.c present")
    chk("engine/common/masterlist.c","NET_MasterQuery","masterlist.c has NET_MasterQuery")
    chk("engine/common/network.c","NA_IP6","IPv6 in network.c")
    print(f"\n{'ALL 24 TESTS PASSED' if ok else 'SOME TESTS FAILED'}")
    return ok

def main():
    if len(sys.argv)<2: print("Usage: patch_engine.py <root>"); sys.exit(1)
    root=sys.argv[1]
    if not os.path.isdir(root): print(f"Not a dir: {root}"); sys.exit(1)
    print(f"Patching: {root}")
    patch_netadr_h(root)
    patch_common_h(root)
    patch_client_h(root)
    patch_network_c(root)
    patch_cl_main(root)
    patch_android_mk(root)
    ok=run_tests(root)
    sys.exit(0 if ok else 1)

if __name__=="__main__":
    main()
