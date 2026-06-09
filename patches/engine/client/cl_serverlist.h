#pragma once
#ifndef CL_SERVERLIST_H
#define CL_SERVERLIST_H
#include "common.h"

#define MAX_FAVORITES  64
#define MAX_HISTORY    32

typedef struct { char address[64]; char hostname[128]; } favorite_entry_t;
typedef struct { char address[64]; }                     history_entry_t;

extern favorite_entry_t g_Favorites[MAX_FAVORITES];
extern int              g_FavoriteCount;
extern history_entry_t  g_History[MAX_HISTORY];
extern int              g_HistoryCount;

void SL_Init( void );
void SL_Shutdown( void );
void SL_History_AddEntry( const char *address );

void SL_Favorites_Add_f( void );
void SL_Favorites_Remove_f( void );
void SL_Favorites_List_f( void );
void SL_History_List_f( void );
void SL_History_Clear_f( void );

#endif
