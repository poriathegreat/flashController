/*
 * flashMemoryController.h
 *
 *  Created on: Nov 18, 2023
 *      Author: PoriaTheGreat
 */

#ifndef INC_FLASHMEMORYCONTROLLER_H_
#define INC_FLASHMEMORYCONTROLLER_H_
#include "stdint.h"




/* To add support for other ICs, they should be added here first. */

/* Only one IC should be uncommented at a time.
 * At the time of writing there was only FLASH IC available for me to test. */
#define IC_W25Qxx




/********************************************************************************/
/*                             Exported Definitions                             */
/********************************************************************************/
/* LIBRARY CONFIGURATIONS */
#define DEBUG_FCTR							(1)
#if DEBUG_FCTR
#define DEBUG_FCTR_REQUESTED_DEBUG_LINES	(10)
#define DEBUG_FCTR_LIBRARY_NAME 			("FLASH CONTROLLER")
#endif

#define FCTR_TOTAL_MEMORYSIZE_BYTES			(4194304*2)
#define FCTR_RESERVED_OFFSET				(1048576)
#define FCTR_SECTOR_SIZE					(4096)
#define FCTR_TOTAL_SECTORS					(FCTR_TOTAL_MEMORYSIZE_BYTES/FCTR_SECTOR_SIZE)
#define FCTR_AVAILABLE_SECTORS				(FCTR_TOTAL_SECTORS - (FCTR_RESERVED_OFFSET/FCTR_SECTOR_SIZE))
#define FCTR_SIZE_OF_KILOBYTE				(1024)

typedef enum {
	FCTRSTAT_OK = 0,
	FCTRSTAT_ERROR = 1,
}fctrStat_t;

/*********************************************************************************/
/*                             Exported Variables                                */
/*********************************************************************************/
/* A global buffer to handle the sector data.
 * The RAM needed to handle these operations is 4096 bytes.
 * This value has been defined globally, to make debugging the situation easier. */
//extern uint8_t fctrSectorBuff[SECTOR_SIZE];
/*********************************************************************************/
/*                              Public Functions                                 */
/*********************************************************************************/


/* This function will take in a sector to write, 4096 bytes total.
 *
 * The memory space of this sector will be chosen automatically.
 * If space was available on the memory, the function will return FCTRSTAT_OK
 * If no space was left on the memory, the function will return FCTRSTAT_ERROR */
fctrStat_t fctr_pushToFlash(uint8_t *pBuffer);


/* This function will read, and return a sector, 4096 bytes total.
 * The memory space of this sector will be chosen automatically.
 *
 * If an UNREAD sector was available on the memory, the function returns FCTRSTAT_OK
 * If no UNREAD sector was found on the memory, the function returns FCTRSTAT_ERROR */
fctrStat_t fctr_popFromFlash(uint8_t *pBuffer);


/* This function is to be placed before the super loop. */
fctrStat_t fctr_firstInit(void);
























#endif /* INC_FLASHMEMORYCONTROLLER_H_ */
