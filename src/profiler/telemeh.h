#include "stdint.h"

void MVM_telemetry_timestamp(intptr_t threadID, const char *description);

unsigned int MVM_telemetry_interval_start(intptr_t threadID, const char *description);
void MVM_telemetry_interval_stop(intptr_t threadID, int intervalID, const char *description);
void MVM_telemetry_interval_annotate(intptr_t subject, int intervalID, const char *description);
void MVM_telemetry_interval_annotate_dynamic(intptr_t subject, int intervalID, char *description);

void MVM_telemetry_init(FILE *outfile);
void MVM_telemetry_finish();
