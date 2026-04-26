/*===================================================================*/
/*                                                                   */
/*  InfoNES_Mapper_030_Diagnostics.h : Mapper 30 diagnostic API       */
/*                                                                   */
/*===================================================================*/

#ifndef INFONES_MAPPER_030_DIAGNOSTICS_H_INCLUDED
#define INFONES_MAPPER_030_DIAGNOSTICS_H_INCLUDED

#include "../InfoNES_Types.h"

#ifndef INFONES_MAPPER30_ENABLE_DIAGNOSTICS
#define INFONES_MAPPER30_ENABLE_DIAGNOSTICS 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

BYTE Map30_DebugGetCount(void);
WORD Map30_DebugGetAddr(int index);
BYTE Map30_DebugGetData(int index);

#ifdef __cplusplus
}
#endif

#endif
