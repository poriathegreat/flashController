# flashController
### A light buffer based flash controller, useful for microcontrollers. Optimized for the W25Q64 flash and STM32f030.


## Description:
A flash buffer handler, with a simple memory map. 
Using this library, the entire memory is filled up, then cleared. 
This minimizes wear on the flash memory.

This library will use about 6.5KB of ROM and 1.8KB of RAM (for W25Q64 with 1MB offset.)

The Goal:
In a system where data was handled in 256 bytes blocks, I had the need to buffer data.
I created a RAM buffer, which would handle the data flow. Then I was limited to the small size of RAM I had. Plus if the system lost power data would be lost. 
Therefore, an external flash is needed to handle the overflowing buffer.
The external flash would need to take blocks of 4096byte data (16 of my 256 RAM blocks).

The Problem:
Flash has a limit on the number of writes it can take.
If you were to manually write to specific addresses, you would very quickly wear out the flash memory.

The Solution:
We need a middleware that would write the entire flash memory before erasing and starting over.
This Library will take a sector after specific offset (needed for OTA updates, saving configurations, etc.) 
This sector (4096 bytes) is formated, so that every byte represents a sector in the memory space.

How it works:
The first sector of the first available MB of the memory should look like this:

    0          1                                                                                                                   4096
    |------------------------------------------------------------------------------------------------------------------------------|
    | MEMCHECK | EACH BYTE REPRESENTS THE STATUS OF A SECTOR IN THE MEMORY SPACE                                                   |
    |------------------------------------------------------------------------------------------------------------------------------|

    BYTE 0:          MEMCHECK must be 0b01010101 . This is a simple way to check the memory has been formated or not.
    BYTE 1 - 4096: 	Each byte will represent a SECTOR in the memory.
   
    If BYTE 0 is not correctly formated, we are on a fresh memory space. We need to format the memory to look the part.
   
    BYTES 1 - 4096:
   
    These bytes will show the status of each SECTOR of the memory.
   
    Each byte of the PAGE status bytes can have the following values:
 
 		0b11111111 means not formated / unknown data.

 		0b01011111 means empty			    -> RAM can be loaded here

 		0b01011110 means full/unread	  -> must be loaded to RAM

 		0b01011100 means full/read		  -> ready to be cleared.

 		other      means corrupted memory.

When writing data we look for the first available EMPRTY sector and we write the data there.
When reading data we look for the first available UNREAD sector, thats the data we load.
Once there are no more EMPTY sectors to write to, we erase all the READ sectors to be EMPTY
On each change, we need to change the corresponding byte. 
The change can be done without erasing the flash, because we are only changing ones to zeros.
This means, the STATUS sector, we used as the memory map, doesnt need to be erased, until the entire
memory fills up. No need to worry about wearing out the memory map sector.





## How to get started: 
### First Step:
Include the flashController.c and flashController.h files in your project.

### Second Step:
In flashController.h, edit the following to meet your needs: (Units are BYTES)

       #define FCTR_TOTAL_MEMORYSIZE_BYTES     (4194304*2)   
       #define FCTR_RESERVED_OFFSET            (1048576)      
       #define FCTR_SECTOR_SIZE                (4096)
       #define FCTR_TOTAL_SECTORS              (FCTR_TOTAL_MEMORYSIZE_BYTES/FCTR_SECTOR_SIZE)
       #define FCTR_AVAILABLE_SECTORS          (FCTR_TOTAL_SECTORS - (FCTR_RESERVED_OFFSET/FCTR_SECTOR_SIZE))
       #define FCTR_SIZE_OF_KILOBYTE           (1024)

### Third Step:
In the flashController.c, the final section is "Middle ware functions to connect to the physical flash hardware" 
These functions are tested to work with the w25qxx library from nimaltd. 
If you prefer using different functions, this is where you need to make the changes. 

### Fourth Step:
Now in the main program, before the superloop, call fctr_firstInit();
This should get the memory space ready.

### Happy Coding :)
Now you just need to call the PUSH and POP functions according to your needs.

This function will push the 4096byte buffer you give, to an empty sector in the flash memory:

     fctr_pushToFlash(yourBuffer)

This function will fetch a sector from the flash memory, and mark that block READ:

     fctr_popFromFlash(yourBuffer)































