#ifndef DUTCHOVEN_BOF_BEACON_H
#define DUTCHOVEN_BOF_BEACON_H

#include <windows.h>

/* Minimal Beacon API surface used by DutchOven; intentionally not a full SDK copy. */
typedef struct {
    char *original;
    char *buffer;
    int length;
    int size;
} datap;

#define CALLBACK_OUTPUT 0x00
#define CALLBACK_ERROR 0x0d

/* Beacon resolves these symbols when the COFF object is loaded. */
DECLSPEC_IMPORT void BeaconDataParse(datap *parser, char *buffer, int size);
DECLSPEC_IMPORT int BeaconDataInt(datap *parser);
DECLSPEC_IMPORT int BeaconDataLength(datap *parser);
DECLSPEC_IMPORT char *BeaconDataExtract(datap *parser, int *size);
DECLSPEC_IMPORT void BeaconPrintf(int type, char *format, ...);
DECLSPEC_IMPORT BOOL BeaconIsAdmin(void);

#endif
