#include "stdint.h"

MVM_PUBLIC void MVM_telemetry_timestamp(MVMThreadContext *threadID, const char *description);

MVM_PUBLIC unsigned int MVM_telemetry_interval_start(MVMThreadContext *threadID, const char *description);
MVM_PUBLIC void MVM_telemetry_interval_stop(MVMThreadContext *threadID, int intervalID, const char *description);
MVM_PUBLIC void MVM_telemetry_interval_annotate(uintptr_t subject, int intervalID, const char *description);
MVM_PUBLIC void MVM_telemetry_interval_annotate_dynamic(uintptr_t subject, int intervalID, char *description);

MVM_PUBLIC void MVM_telemetry_init(FILE *outfile);
MVM_PUBLIC void MVM_telemetry_finish();
