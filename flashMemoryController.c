/*
 * flashMemoryController.c
 *
 *  Created on: Nov 18, 2023
 *      Author: PoriaTheGreat
 *
 *
 * This library will help us automatically manage an EXTERNAL FLASH memory.
 *
 * Flash memory has a well known limitation on the number of writes it can handle before its first failure.
 *
 * To address this problem we need a flash controller layer that will circle the memory space with least wear
 * on the memory hardware. */



/* The first sector of the first available MB of the memory should look like this:
 *
 * 0          1                                                                                                                        4096
 * |------------------------------------------------------------------------------------------------------------------------------------|
 * | MEMCHECK | EACH BYTE REPRESENTS THE STATUS OF A SECTOR IN THE MEMORY SPACE                                                         |
 * |------------------------------------------------------------------------------------------------------------------------------------|
 *
 * BYTE 0:          MEMCHECK must be 0b01010101 . This is a simple way to check the memory has been formated or not.
 * BYTE 1 - 4096: 	Each byte will represent a SECTOR in the memory.
 *
 * If BYTE 0 is not correctly formated, we are on a fresh memory space. We need to format the memory to look the part.
 *
 *
 * BYTES 1 - 4096:
 *
 * These bytes will show the status of each SECTOR of the memory.
 *
 * Each byte of the PAGE status bytes can have the following values:
 * 		0b11111111 means not formated / unknown data.
 *
 * 		0b01011111 means empty			-> RAM can be loaded here
 *
 * 		0b01011110 means full/unread	-> must be loaded to RAM
 *
 * 		0b01011100 means full/read		-> ready to be cleared.
 *
 * 		other      means corrupted memory.
 *
 * When writing data we look for the first available empty SECTOR and we write the data there.
 * When reading data we look for the first available full/unread SECTOR, thats the data we load.
 * Once there are no more EMPTY sectors to write to, we erase all the READ sectors to be EMPTY
 *
 * On each change, we need to change the corresponding byte.
 * */
/********************************************************************************/
/*                                  Includes                                    */
/********************************************************************************/
#include "flashMemoryController.h"

#if DEBUG_FCTR
#include "debug.h"
#endif


#ifdef IC_W25Qxx
#include "w25qxx.h"
#endif


#define SIGNATURE_BYTE				(0b01010101)

/********************************************************************************/
/*                            Private definitions                               */
/********************************************************************************/

typedef enum{
	FCTR_SECTOR_RESERVED    = 0b11111111,
	FCTR_SECTOR_EMPTY 		= 0b01011111,
	FCTR_SECTOR_UNREAD		= 0b01011110,
	FCTR_SECTOR_READ		= 0b01011100,
}fctrSectorStat_t;

/********************************************************************************/
/*                             Private functions                                */
/********************************************************************************/
fctrStat_t fctr_initIC (void);
fctrStat_t fctr_getFlashSizes (uint32_t *totalSpaceInKB, uint32_t *totalSectors, uint32_t *sectorSize);

fctrStat_t fctr_writeByte(uint8_t byteToWrite, uint32_t requestedAddress);

fctrStat_t fctr_writeSector(uint8_t *pBuffer, uint32_t requestedSector);
fctrStat_t fctr_readSector(uint8_t *pBuffer, uint32_t requestedSector);
fctrStat_t fctr_eraseSector(uint32_t sectorToErase);

fctrStat_t fctr_writeSectorStatus(uint8_t *sectorStatus);
fctrStat_t fctr_readSectorStatus(uint8_t *sectorStatus);

fctrStat_t fctr_findSectorToWrite(uint32_t *sectorToWrite);
fctrStat_t fctr_findSectorToRead(uint32_t *sectorToRead);

fctrStat_t fctr_changeSectorStatus(fctrSectorStat_t newSectorStat, uint32_t sectorToChangeStatusOf);
fctrStat_t fctr_formatTheMemorySpace(void);

/********************************************************************************/
/*                             Private Variables                                */
/********************************************************************************/
#if DEBUG_FCTR
uint8_t fctr_debugSpace = 0;
#endif

//uint8_t fctrSectorBuff[FCTR_SECTOR_SIZE] = {0};	/* We need to write a sector at a time because we only know the per sector status. */
uint8_t fctrStatusBytes[FCTR_AVAILABLE_SECTORS] = {0}; /* We load the memory status, one page at a time. */

/********************************************************************************/
/*                         Main functions of the library.                       */
/********************************************************************************/
/* This function will take in a sector to write, 4096 bytes total.
 *
 * The memory space of this sector will be chosen automatically.
 * If space was available on the memory, the function will return FCTRSTAT_OK
 * If no space was left on the memory, the function will return FCTRSTAT_ERROR */
fctrStat_t fctr_pushToFlash(uint8_t *pBuffer){
	fctrStat_t result = FCTRSTAT_ERROR;

	uint32_t sectorToWrite = RESET;


	if (fctr_findSectorToWrite(&sectorToWrite) != FCTRSTAT_OK){
		return FCTRSTAT_ERROR;
	}

	if(fctr_writeSector(pBuffer, sectorToWrite) != FCTRSTAT_OK){
		return FCTRSTAT_ERROR;
	}


	if(fctr_changeSectorStatus(FCTR_SECTOR_UNREAD, sectorToWrite) != FCTRSTAT_OK){
		return FCTRSTAT_ERROR;
	}

	result = FCTRSTAT_OK;
	return result;
}

/* This function will read, and return a sector, 4096 bytes total.
 * The memory space of this sector will be chosen automatically.
 *
 * If an UNREAD sector was available on the memory, the function returns FCTRSTAT_OK
 * If no UNREAD sector was found on the memory, the function returns FCTRSTAT_ERROR */
fctrStat_t fctr_popFromFlash(uint8_t *pBuffer){
	fctrStat_t result = FCTRSTAT_ERROR;

	uint32_t sectorToRead = RESET;


	if (fctr_findSectorToRead(&sectorToRead) != FCTRSTAT_OK){
		return FCTRSTAT_ERROR;
	}

	if(fctr_readSector(pBuffer, sectorToRead) != FCTRSTAT_OK){
		return FCTRSTAT_ERROR;
	}


	if(fctr_changeSectorStatus(FCTR_SECTOR_READ, sectorToRead) != FCTRSTAT_OK){
		return FCTRSTAT_ERROR;
	}

	result = FCTRSTAT_OK;
	return result;
}


/* This function is to be placed before the super loop. */
fctrStat_t fctr_firstInit(void){
	fctrStat_t result = FCTRSTAT_ERROR;

#if DEBUG_FCTR
	fctr_debugSpace = console_requestSpace(DEBUG_FCTR_REQUESTED_DEBUG_LINES, DEBUG_FCTR_LIBRARY_NAME);
#endif

	/* We need to initialize the hardware we're using. */
	if(fctr_initIC() != FCTRSTAT_OK){
#if DEBUG_FCTR
		console_printf(fctr_debugSpace++, CONSOLE_PART_ONE, "Flash Hardware Init Failed!");
#endif
		return FCTRSTAT_ERROR;
	}

	/* Now we need to request the total size of the memory in KB. */
	uint32_t totalSizeKB = RESET;
	uint32_t sectorSize = RESET;
	uint32_t sectorCount = RESET;
	if(fctr_getFlashSizes(&totalSizeKB, &sectorCount, &sectorSize) != FCTRSTAT_OK ){
#if DEBUG_FCTR
		console_printf(fctr_debugSpace++, CONSOLE_PART_ONE, "A problem with the FLASH SIZE is found.");

		if(totalSizeKB != (FCTR_TOTAL_MEMORYSIZE_BYTES/FCTR_SIZE_OF_KILOBYTE)){
			console_printf(fctr_debugSpace++, CONSOLE_PART_ONE, "ERROR!!! TOTAL SIZE DETECTED = %d but TOTAL SIZE SET in library is: %d. PROCEEDING WITH [%d]",
					totalSizeKB, (FCTR_TOTAL_MEMORYSIZE_BYTES/FCTR_SIZE_OF_KILOBYTE), (FCTR_TOTAL_MEMORYSIZE_BYTES/FCTR_SIZE_OF_KILOBYTE) );
		}

		if(sectorSize != FCTR_SECTOR_SIZE){
			console_printf(fctr_debugSpace++, CONSOLE_PART_ONE, "ERROR!!! SECTOR SIZE DETECTED = %d but SECTOR SIZE SET in library is: %d. PROCEEDING WITH [%d]",
					(sectorSize), (FCTR_SECTOR_SIZE), (FCTR_SECTOR_SIZE) );

		}
#endif
		return FCTRSTAT_ERROR;
	}
#if DEBUG_FCTR
	console_printf(fctr_debugSpace++, CONSOLE_PART_ONE, "Total Flash Space: [ %d KB]", totalSizeKB);
	console_printf(fctr_debugSpace++, CONSOLE_PART_ONE, "Total Available: [ %d KB]", totalSizeKB-(FCTR_RESERVED_OFFSET/FCTR_SIZE_OF_KILOBYTE));
#endif

	/* We now check the first sector after the offset.
	 * This sector will tell us exactly how we need to proceed. */
	if(fctr_readSectorStatus(fctrStatusBytes) != FCTRSTAT_OK){
		return FCTRSTAT_ERROR;
	}


	/* This is just a simple signature byte, to show this library has been used on this memory before or not.
	 * if this byte is changed, there must be something wrong with the memory. */
	if ( fctrStatusBytes[0] != SIGNATURE_BYTE){
		/* If the signature is wrong, we format the memoryMap sector. */
#if DEBUG_FCTR
		console_printf(fctr_debugSpace++, CONSOLE_PART_ONE, "Signature Byte not found!!!! FORMATING MEMORY!!!");
#endif
		fctr_formatTheMemorySpace();
		if(fctr_readSector(fctrStatusBytes, 0) != FCTRSTAT_OK){
			return FCTRSTAT_ERROR;
		}
	}

//	/* total amount of sectors. */
//	memcpy(fctrStatusBytes, fctrSectorBuff, AVAILABLE_SECTORS);
	result = FCTRSTAT_OK;

	/* So now we have the status of every needed sector loaded into RAM. */
	return result;
}


/********************************************************************************/
/*                       Internal functions of the library.                     */
/********************************************************************************/

/* In this function we will take the entire SECTOR that holds the memory addresses and refresh it.
 * This will only happen when there are no more EMPTY sectors left on the flash memory.
 * All of the READ sectors will be set to EMPTY at once. */
fctrStat_t fctr_refreshTheMemoryStatus(uint8_t *memStatus){
	fctrStat_t result = FCTRSTAT_ERROR;

	/* Don't forget the signature... */
	memStatus[0] = SIGNATURE_BYTE;

	/* The STATUS sector is the first sector. (after the offset) */
	if(fctr_writeSectorStatus(memStatus) != FCTRSTAT_OK){
		return FCTRSTAT_ERROR;
	}

	result = FCTRSTAT_OK;
	return result;
}

/* This function will format the sector space so we know which bytes are used and which are not used. */
fctrStat_t fctr_formatTheMemorySpace(void){
	fctrStat_t result = FCTRSTAT_ERROR;

	/* We set the bytes we care about.
	 * We will set everything to EMPTY. */
	memset (fctrStatusBytes, FCTR_SECTOR_EMPTY, FCTR_AVAILABLE_SECTORS );

	/* Don't forget the signature... */
	fctrStatusBytes[0] = SIGNATURE_BYTE;

	/* The STATUS sector is the first sector. (after the offset) */
	if(fctr_writeSectorStatus(fctrStatusBytes) != FCTRSTAT_OK){
		return FCTRSTAT_ERROR;
	}
	result = FCTRSTAT_OK;

	return result;
}




/* This function will change the sector status of a BYTE.
 * In flash memory, when writing a BYTE of data, only 1s change to 0s,
 *
 * We have kept this in mind when choosing STATUS values for EMPTY to UNREAD or from UNREAD to READ.
 *
 * IF anything does go wrong, if enabled, the DEBUG will show. */
fctrStat_t fctr_changeSectorStatus(fctrSectorStat_t newSectorStat, uint32_t sectorToChangeStatusOf){
	fctrStat_t result = FCTRSTAT_ERROR;

	/* This sector status is actually written exactly at:
	 * FCTR_RESERVED_OFFSET + sector */
	uint8_t byteBuff = newSectorStat;
	if (fctr_writeByte( byteBuff,  sectorToChangeStatusOf ) != FCTRSTAT_OK){
		result = FCTRSTAT_ERROR;
	}else {
		result = FCTRSTAT_OK;
	}
	fctrStatusBytes[sectorToChangeStatusOf] = byteBuff;
	return result;
}


/* This function will look for a UNREAD sector and return its sector.
 * if ERROR is returned, it means no UNREAD sector was found.*/
fctrStat_t fctr_findSectorToRead(uint32_t *sectorToRead){
	fctrStat_t result = FCTRSTAT_ERROR;


	for(uint32_t unreadSector = 1; unreadSector < (FCTR_AVAILABLE_SECTORS); unreadSector++){
		if(fctrStatusBytes[unreadSector] == FCTR_SECTOR_UNREAD){
			*sectorToRead = unreadSector;
#if DEBUG_FCTR
			console_printf(fctr_debugSpace, CONSOLE_PART_ONE, "The UNREAD sector loaded is: [%d]   ", *sectorToRead);
#endif
			return FCTRSTAT_OK;
		}
	}

	/* If we reach here, it means we have not found an unread sector. */
	result = FCTRSTAT_ERROR;
	return result;
}



/* This function will first look for an EMPTY sector.
 * If not found, it means all the sectors have been written in,
 * so we set every written sector (READ) to EMPTY, only keeping the sectors
 * with UNREAD data. */
fctrStat_t fctr_findSectorToWrite(uint32_t *sectorToWrite){
	fctrStat_t result = FCTRSTAT_ERROR;

	/* We need to start from sector 1, because sector 0 is the memory map table. */
	for(uint32_t emptySector = 1; emptySector < (FCTR_AVAILABLE_SECTORS); emptySector++){
		if(fctrStatusBytes[emptySector] == FCTR_SECTOR_EMPTY){
			*sectorToWrite = emptySector;
#if DEBUG_FCTR
			console_printf(fctr_debugSpace+1, CONSOLE_PART_ONE, "The EMPTY sector loaded is: [%d]   ", *sectorToWrite);
#endif
			return FCTRSTAT_OK;
		}
	}

#if DEBUG_FCTR
	console_printf(fctr_debugSpace+1, CONSOLE_PART_ONE, "No more EMPTY sectors, resetting READ sectors..   ");
#endif

	/* If we reach here, it means we have not found an empty sector.
	 * We will now take a look at the memory space, and erase all the
	 * "READ" sectors. We will keep the UNREAD sectors untouched. */
	_Bool foundASector = RESET;
	for(uint32_t readSector = 1; readSector < (FCTR_AVAILABLE_SECTORS); readSector++){
		if(fctrStatusBytes[readSector] != FCTR_SECTOR_UNREAD){
			fctrStatusBytes[readSector] = FCTR_SECTOR_EMPTY;
			foundASector = SET;
		}
	}

	/* If the memory was not entirely full, and we cleared some READ sectors: */
	if(foundASector){
		/* We refresh the status bytes saved in the memory. */
		if( fctr_refreshTheMemoryStatus(fctrStatusBytes) != FCTRSTAT_OK ){
			return FCTRSTAT_ERROR;
		}
#if DEBUG_FCTR
				console_printf(fctr_debugSpace+5, CONSOLE_PART_FOUR, ".... [DONE]  ");
#endif

		/* Now we look for an EMPTY sector again. */
		for(uint32_t i = RESET; i < (FCTR_AVAILABLE_SECTORS); i++){
			if(fctrStatusBytes[i] == FCTR_SECTOR_EMPTY){
				*sectorToWrite = i;
#if DEBUG_FCTR
				console_printf(fctr_debugSpace+1, CONSOLE_PART_ONE, "The EMPTY sector loaded is: [%d]   ", *sectorToWrite);
#endif
				return FCTRSTAT_OK;
			}
		}
	}

	/* IF we reach here, it means the entire memory was UNREAD */
	result = FCTRSTAT_ERROR;
	return result;
}



/*********************************************************************************/
/*       Middle ware functions to connect to the physical flash hardware         */
/*********************************************************************************/

/* We need to initialize the chip we are using. */
fctrStat_t fctr_initIC(void){
	fctrStat_t result = FCTRSTAT_ERROR;
	/* This is the hardware layer of the code, these parts need to be edited to
	 * handle the IC we are targeting. */
#ifdef IC_W25Qxx
	if (W25qxx_Init() == true){
		result =  FCTRSTAT_OK;
	}else {
		result =  FCTRSTAT_ERROR;
	}
#endif /* IC_W25Qxx */
	return result;
}


/* To know what we are dealing with, we need to know the total memory space we have.
 * This function should return:
 * The total flash memory size in kilobytes.
 * Total count of sectors.
 * Sector size in bytes.
 * */
fctrStat_t fctr_getFlashSizes (uint32_t *totalSpaceInKB, uint32_t *totalSectors, uint32_t *sectorSize){
	fctrStat_t result = FCTRSTAT_ERROR;

#ifdef IC_W25Qxx
	/* This is the hardware layer of the code, these parts need to be edited to
	 * handle the IC we are targeting. */
	*totalSpaceInKB = w25qxx.CapacityInKiloByte;
	*totalSectors = w25qxx.SectorCount;
	*sectorSize = w25qxx.SectorSize;

	if(*totalSpaceInKB < FCTR_SIZE_OF_KILOBYTE){
		/* The memory space is less than a MB and this library is not needed for it.
		 * The first MB of memory is usually used to save configuration and enable OTA update. */
		result = FCTRSTAT_ERROR;
	}else {
		result = FCTRSTAT_OK;
	}
#endif /* IC_W25Qxx */

	return result;
}


/* A sector number should be given.
 * Sector 0, will be the first sector after the FCTR_RESERVED_OFFSET.
 * This function will return SECTOR0, the status sector.  */
fctrStat_t fctr_readSectorStatus(uint8_t *sectorStatus){
	fctrStat_t result = FCTRSTAT_ERROR;

#ifdef IC_W25Qxx
	/* This is the hardware layer of the code, these parts need to be edited to
	 * handle the IC we are targeting. */
	/* This will return the sectorStatus, skipping the FCTR_RESERVED_OFFSET of memory. */
	W25qxx_ReadSector(sectorStatus, (FCTR_RESERVED_OFFSET/FCTR_SECTOR_SIZE), 0, FCTR_AVAILABLE_SECTORS);
	result = FCTRSTAT_OK;
#endif /* IC_W25Qxx */

	return result;
}


/* A sector number should be given.
 * This sector number will skip the FCTR_RESERVED_OFFSET of memory.
 * Sector 0, will be the first sector after the FCTR_RESERVED_OFFSET. */
fctrStat_t fctr_readSector(uint8_t *pBuffer, uint32_t requestedSector){
	fctrStat_t result = FCTRSTAT_ERROR;

#ifdef IC_W25Qxx
	/* We need to skip the FCTR_RESERVED_OFFSET of memory. */
	uint32_t newSectorAddress = (FCTR_RESERVED_OFFSET/FCTR_SECTOR_SIZE) + requestedSector;

	/* This is the hardware layer of the code, these parts need to be edited to
	 * handle the IC we are targeting. */
	/* This will return a sector, skipping the FCTR_RESERVED_OFFSET of memory. */
	W25qxx_ReadSector(pBuffer, newSectorAddress, 0, FCTR_SECTOR_SIZE);
	/* No integrity check is available from the previous layer... */
	result = FCTRSTAT_OK;
#endif /* IC_W25Qxx */

	return result;
}


/* A sector number should be given.
 * This sector number will skip the FCTR_RESERVED_OFFSET of memory.
 * Sector 0, will be the first sector after the FCTR_RESERVED_OFFSET. */
fctrStat_t fctr_writeSector(uint8_t *pBuffer, uint32_t requestedSector){
	fctrStat_t result = FCTRSTAT_ERROR;

#ifdef IC_W25Qxx
	/* We need to skip the FCTR_RESERVED_OFFSET of memory. */
	uint32_t newSectorAddress = (FCTR_RESERVED_OFFSET/FCTR_SECTOR_SIZE) + requestedSector;

	/* This is the hardware layer of the code, these parts need to be edited to
	 * handle the IC we are targeting. */
	/* This will return a sector, skipping the FCTR_RESERVED_OFFSET of memory. */
	if( W25qxx_IsEmptySector(newSectorAddress, 0, FCTR_SECTOR_SIZE) ){
		W25qxx_WriteSector(pBuffer,newSectorAddress,0, FCTR_SECTOR_SIZE);
	}else {
		W25qxx_EraseSector(newSectorAddress);
		W25qxx_WriteSector(pBuffer,newSectorAddress,0, FCTR_SECTOR_SIZE);
	}

	/* No integrity check is available from the previous layer... */
	result = FCTRSTAT_OK;
#endif /* IC_W25Qxx */

	return result;
}

/* A sector number should be given.
 * This sector number will skip the FCTR_RESERVED_OFFSET of memory.
 * Sector 0, will be the first sector after the FCTR_RESERVED_OFFSET.
 * This function takes in an address end as well. */
fctrStat_t fctr_writeSectorStatus(uint8_t *sectorStatus){
	fctrStat_t result = FCTRSTAT_ERROR;

#ifdef IC_W25Qxx
	/* This is the hardware layer of the code, these parts need to be edited to
	 * handle the IC we are targeting. */
	/* This will return a sector, skipping the FCTR_RESERVED_OFFSET of memory. */

	W25qxx_EraseSector( (FCTR_RESERVED_OFFSET/FCTR_SECTOR_SIZE) );
	W25qxx_WriteSector(sectorStatus, (FCTR_RESERVED_OFFSET/FCTR_SECTOR_SIZE) ,0, FCTR_AVAILABLE_SECTORS);

	/* No integrity check is available from the previous layer... */
	result = FCTRSTAT_OK;
#endif /* IC_W25Qxx */

	return result;
}

/* A sector number should be given.
 * This sector number will skip the FCTR_RESERVED_OFFSET of memory.
 * Sector 0, will be the first sector after the FCTR_RESERVED_OFFSET. */
fctrStat_t fctr_eraseSector(uint32_t sectorToErase){
	fctrStat_t result = FCTRSTAT_ERROR;

#ifdef IC_W25Qxx
	W25qxx_EraseSector( (FCTR_RESERVED_OFFSET/FCTR_SECTOR_SIZE) + sectorToErase);
	result = FCTRSTAT_OK;
#endif /* IC_W25Qxx */

	return result;
}


/* An ADDRESS should be given.
 * This address will skip the FCTR_RESERVED_OFFSET of memory.
 * ADDRESS 0, will be the first byte after the FCTR_RESERVED_OFFSET. */
fctrStat_t fctr_writeByte(uint8_t byteToWrite, uint32_t requestedAddress){
	fctrStat_t result = FCTRSTAT_ERROR;

#ifdef IC_W25Qxx
	/* This is the hardware layer of the code, these parts need to be edited to
	 * handle the IC we are targeting. */
	/* We need to skip the FCTR_RESERVED_OFFSET of memory.
	 * This will write a byte to the memory.
	 * WARNING: WRITING A BYTE WILL ONLY CHANGE 1s TO 0s!!!!!!!!!*/
	W25qxx_WriteByte( byteToWrite, (FCTR_RESERVED_OFFSET + requestedAddress) );
	uint8_t testWrittenByte = RESET;
	W25qxx_ReadByte( &testWrittenByte, (FCTR_RESERVED_OFFSET + requestedAddress) );

	/* We can check if the byte we wrote worked or not... */
	if (testWrittenByte == byteToWrite){
		result = FCTRSTAT_OK;
	}else {

#if DEBUG_FCTR
		console_printf(fctr_debugSpace+2, CONSOLE_PART_ONE, "Failed to write byte [%lu]: W[%X] != R[%X]  ", (FCTR_RESERVED_OFFSET + requestedAddress), byteToWrite, testWrittenByte);
#endif
		result = FCTRSTAT_ERROR;
	}
#endif /* IC_W25Qxx */
	return result;
}


































