/*
cl_serverlist.c — server browser: favourites + history
Uses old engine FS_ / BF_ / Q_ APIs directly.
Written for tyabus/xash3d 2024.
*/
#include "common.h"
#include "client.h"
#include "cl_serverlist.h"

favorite_entry_t g_Favorites[MAX_FAVORITES];
int              g_FavoriteCount = 0;
history_entry_t  g_History[MAX_HISTORY];
int              g_HistoryCount  = 0;

/* ------------------------------------------------------------------ */
/*  Favorites                                                           */
/* ------------------------------------------------------------------ */

static void Favs_Load( void )
{
	char *raw, *p, line[256];
	int   len;

	g_FavoriteCount = 0;
	raw = (char *)FS_LoadFile( "favorites.cfg", NULL, true );
	if( !raw ) return;

	p = raw;
	while( *p && g_FavoriteCount < MAX_FAVORITES )
	{
		while( *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' ) p++;
		if( !*p ) break;
		len = 0;
		while( *p && *p != '\r' && *p != '\n' && len < 255 )
			line[len++] = *p++;
		line[len] = '\0';
		if( !len ) continue;
		{
			char addr[64] = "", host[128] = "";
			char *sp = strchr( line, ' ' );
			if( sp ) { Q_strncpy( addr, line, sp-line+1 ); Q_strncpy( host, sp+1, 128 ); }
			else       Q_strncpy( addr, line, 64 );
			if( addr[0] )
			{
				Q_strncpy( g_Favorites[g_FavoriteCount].address,  addr, 64  );
				Q_strncpy( g_Favorites[g_FavoriteCount].hostname, host, 128 );
				g_FavoriteCount++;
			}
		}
	}
	Mem_Free( raw );
}

static void Favs_Save( void )
{
	file_t *f = FS_Open( "favorites.cfg", "w", true );
	int     i;
	if( !f ) return;
	for( i = 0; i < g_FavoriteCount; i++ )
		FS_Printf( f, "%s %s\n",
			g_Favorites[i].address, g_Favorites[i].hostname );
	FS_Close( f );
}

static void Favs_Add( const char *address, const char *hostname )
{
	int i;
	for( i = 0; i < g_FavoriteCount; i++ )
		if( !Q_stricmp( g_Favorites[i].address, address ) )
		{
			if( hostname && hostname[0] )
				Q_strncpy( g_Favorites[i].hostname, hostname, 128 );
			Favs_Save();
			return;
		}
	if( g_FavoriteCount >= MAX_FAVORITES )
	{ Con_Printf( "Favorites full.\n" ); return; }
	Q_strncpy( g_Favorites[g_FavoriteCount].address,  address,            64  );
	Q_strncpy( g_Favorites[g_FavoriteCount].hostname, hostname ? hostname : "", 128 );
	g_FavoriteCount++;
	Favs_Save();
	Con_Printf( "Added %s to favorites.\n", address );
}

/* ------------------------------------------------------------------ */
/*  History                                                             */
/* ------------------------------------------------------------------ */

static void Hist_Load( void )
{
	char *raw, *p, line[256];
	int   len;

	g_HistoryCount = 0;
	raw = (char *)FS_LoadFile( "history.cfg", NULL, true );
	if( !raw ) return;

	p = raw;
	while( *p && g_HistoryCount < MAX_HISTORY )
	{
		while( *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' ) p++;
		if( !*p ) break;
		len = 0;
		while( *p && *p != '\r' && *p != '\n' && len < 63 )
			line[len++] = *p++;
		line[len] = '\0';
		if( !len ) continue;
		Q_strncpy( g_History[g_HistoryCount++].address, line, 64 );
	}
	Mem_Free( raw );
}

static void Hist_Save( void )
{
	file_t *f = FS_Open( "history.cfg", "w", true );
	int     i;
	if( !f ) return;
	for( i = 0; i < g_HistoryCount; i++ )
		FS_Printf( f, "%s\n", g_History[i].address );
	FS_Close( f );
}

void SL_History_AddEntry( const char *address )
{
	int i, j;
	if( !address || !address[0] ) return;
	/* remove duplicate */
	for( i = 0; i < g_HistoryCount; i++ )
		if( !Q_stricmp( g_History[i].address, address ) )
		{
			for( j = i; j < g_HistoryCount - 1; j++ )
				g_History[j] = g_History[j+1];
			g_HistoryCount--;
			break;
		}
	if( g_HistoryCount >= MAX_HISTORY ) g_HistoryCount = MAX_HISTORY - 1;
	for( i = g_HistoryCount; i > 0; i-- ) g_History[i] = g_History[i-1];
	Q_strncpy( g_History[0].address, address, 64 );
	g_HistoryCount++;
	Hist_Save();
}

/* ------------------------------------------------------------------ */
/*  Console commands                                                    */
/* ------------------------------------------------------------------ */

void SL_Favorites_Add_f( void )
{
	if( Cmd_Argc() < 2 ) { Con_Printf("Usage: favorites_add <ip:port> [name]\n"); return; }
	Favs_Add( Cmd_Argv(1), Cmd_Argc() >= 3 ? Cmd_Argv(2) : NULL );
}
void SL_Favorites_Remove_f( void )
{
	int idx, i;
	if( Cmd_Argc() < 2 ) { Con_Printf("Usage: favorites_remove <index>\n"); return; }
	idx = Q_atoi( Cmd_Argv(1) ) - 1;
	if( idx < 0 || idx >= g_FavoriteCount ) { Con_Printf("Bad index.\n"); return; }
	for( i = idx; i < g_FavoriteCount - 1; i++ ) g_Favorites[i] = g_Favorites[i+1];
	g_FavoriteCount--;
	Favs_Save();
	Con_Printf( "Removed favorite #%d.\n", idx+1 );
}
void SL_Favorites_List_f( void )
{
	int i;
	Con_Printf( "Favorites (%d):\n", g_FavoriteCount );
	for( i = 0; i < g_FavoriteCount; i++ )
		Con_Printf( "  %d. %s  %s\n", i+1, g_Favorites[i].address, g_Favorites[i].hostname );
}
void SL_History_List_f( void )
{
	int i;
	Con_Printf( "History (%d):\n", g_HistoryCount );
	for( i = 0; i < g_HistoryCount; i++ )
		Con_Printf( "  %d. %s\n", i+1, g_History[i].address );
}
void SL_History_Clear_f( void )
{
	g_HistoryCount = 0;
	Hist_Save();
	Con_Printf( "History cleared.\n" );
}

/* ------------------------------------------------------------------ */
/*  Init / Shutdown                                                     */
/* ------------------------------------------------------------------ */

void SL_Init( void )
{
	Favs_Load();
	Hist_Load();
	Cmd_AddCommand( "favorites_add",    SL_Favorites_Add_f,    "add server to favourites" );
	Cmd_AddCommand( "favorites_remove", SL_Favorites_Remove_f, "remove from favourites" );
	Cmd_AddCommand( "favorites_list",   SL_Favorites_List_f,   "list favourites" );
	Cmd_AddCommand( "history_list",     SL_History_List_f,     "list connection history" );
	Cmd_AddCommand( "history_clear",    SL_History_Clear_f,    "clear connection history" );
	MsgDev( D_NOTE, "SL_Init: %d favs, %d history\n", g_FavoriteCount, g_HistoryCount );
}

void SL_Shutdown( void )
{
	Favs_Save();
	Hist_Save();
}
