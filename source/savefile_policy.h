#ifndef SAVEFILE_POLICY_H
#define SAVEFILE_POLICY_H

#include "ff.h"

/* Only a missing directory entry proves that a new save should be created. */
static inline int SaveFileOpenMeansMissing(FRESULT result)
{
	return result == FR_NO_FILE;
}

#endif
