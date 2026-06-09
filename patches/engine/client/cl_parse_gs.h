#pragma once
#ifndef CL_PARSE_GS_H
#define CL_PARSE_GS_H
#include "common.h"
void Delta_ParseTableField_GS( sizebuf_t *msg );
void CL_ParseGoldSrcStatusMessage( netadr_t from, sizebuf_t *msg, qboolean legacy );
void CL_ParseGoldSrcServerMessage( sizebuf_t *msg );
#endif
