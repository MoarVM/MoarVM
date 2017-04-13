#include "stdint.h"

void takeTimeStamp(intptr_t threadID, const char *description);

unsigned int startInterval(intptr_t threadID, const char *description);
void stopInterval(intptr_t threadID, int intervalID, const char *description);
void annotateInterval(intptr_t subject, int intervalID, const char *description);
void annotateIntervalDynamic(intptr_t subject, int intervalID, char *description);

void initTelemetry(FILE *outfile);
void finishTelemetry();
