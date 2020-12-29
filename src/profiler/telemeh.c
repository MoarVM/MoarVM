#include <moar.h>

#ifdef HAVE_TELEMEH

#include <stdio.h>
#include <time.h>
#include <string.h>

#ifdef MVM_HAS_RDTSCP
# ifdef _WIN32
#  include <intrin.h>
# else
#  if defined(__x86_64__) || defined(__i386__)
#   include <x86intrin.h>
#  else
unsigned int __rdtscp(unsigned int *inval) {
    *inval = 0;
    return 0;
}
#  endif
# endif
#else
unsigned int __rdtscp(unsigned int *inval) {
    *inval = 0;
    return 0;
}
#endif

double ticksPerSecond;

// use RDTSCP instruction to get the required pipeline flush implicitly
#define READ_TSC(tscValue) \
{ \
    unsigned int _tsc_aux; \
    tscValue = __rdtscp(&_tsc_aux); \
}

#ifdef __clang__
#if !__has_builtin(__builtin_ia32_rdtscp)
#undef READ_TSC
#define READ_TSC(tscValue) { tscValue = 0; }
#warning "not using rdtscp"
#endif
#endif

#ifdef _WIN32
#include <windows.h>
#define MVM_sleep(ms) do { Sleep(ms); } while (0)
#else
#include <unistd.h>
#define MVM_sleep(ms) do { usleep(ms * 1000); } while (0)
#endif

enum RecordType {
    Calibration,
    Epoch,
    TimeStamp,
    IntervalStart,
    IntervalEnd,
    IntervalAnnotation,
    DynamicString
};

struct CalibrationRecord {
    double ticksPerSecond;
};

struct EpochRecord {
    unsigned long long time;
};

struct TimeStampRecord {
    unsigned long long time;
    const char *description;
};

struct IntervalRecord {
    unsigned long long time;
    unsigned int intervalID;
    const char *description;
};

struct IntervalAnnotation {
    unsigned int intervalID;
    const char *description;
};

struct DynamicString {
    unsigned int intervalID;
    char *description;
};

struct TelemetryRecord {
    enum RecordType recordType;

    uintptr_t threadID;

    union {
        struct CalibrationRecord calibration;
        struct EpochRecord epoch;
        struct TimeStampRecord timeStamp;
        struct IntervalRecord interval;
        struct IntervalAnnotation annotation;
        struct DynamicString annotation_dynamic;
    } u;
};

#define RECORD_BUFFER_SIZE 10000

// this is a ring buffer of telemetry events
static struct TelemetryRecord recordBuffer[RECORD_BUFFER_SIZE];
static AO_t recordBufferIndex = 0;
static unsigned int lastSerializedIndex = 0;
static unsigned long long beginningEpoch = 0;
static unsigned int telemetry_active = 0;

struct TelemetryRecord *newRecord()
{
    AO_t newBufferIndex, recordIndex;
    struct TelemetryRecord *record;

    do {
        recordIndex = MVM_load(&recordBufferIndex);
        newBufferIndex = (recordBufferIndex + 1) % RECORD_BUFFER_SIZE;
    } while(!MVM_trycas(&recordBufferIndex, recordIndex, newBufferIndex));

    record = &recordBuffer[recordIndex];
    return record;
}

static unsigned int intervalIDCounter = 0;

MVM_PUBLIC void MVM_telemetry_timestamp(MVMThreadContext *threadID, const char *description)
{
    struct TelemetryRecord *record;

    if (!telemetry_active) { return; }

    record = newRecord();

    READ_TSC(record->u.timeStamp.time);
    record->recordType = TimeStamp;
    record->threadID = (uintptr_t)threadID;
    record->u.timeStamp.description = description;
}

MVM_PUBLIC unsigned int MVM_telemetry_interval_start(MVMThreadContext *threadID, const char *description)
{
    struct TelemetryRecord *record;

    unsigned int intervalID;

    if (!telemetry_active) { return 0; }

    record = newRecord();
    MVM_incr(&intervalIDCounter);
    intervalID = MVM_load(&intervalIDCounter);
    READ_TSC(record->u.interval.time);

    record->recordType = IntervalStart;
    record->threadID = (uintptr_t)threadID;
    record->u.interval.intervalID = intervalID;
    record->u.interval.description = description;

    return intervalID;
}

MVM_PUBLIC void MVM_telemetry_interval_stop(MVMThreadContext *threadID, int intervalID, const char *description)
{
    struct TelemetryRecord *record;

    if (!telemetry_active) { return; }

    record = newRecord();
    READ_TSC(record->u.interval.time);

    record->recordType = IntervalEnd;
    record->threadID = (uintptr_t)threadID;
    record->u.interval.intervalID = intervalID;
    record->u.interval.description = description;
}

MVM_PUBLIC void MVM_telemetry_interval_annotate(uintptr_t subject, int intervalID, const char *description) {
    struct TelemetryRecord *record;

    if (!telemetry_active) { return; }

    record = newRecord();
    record->recordType = IntervalAnnotation;
    record->threadID = subject;
    record->u.annotation.intervalID = intervalID;
    record->u.annotation.description = description;
}

MVM_PUBLIC void MVM_telemetry_interval_annotate_dynamic(uintptr_t subject, int intervalID, char *description) {
    struct TelemetryRecord *record = NULL;

    if (!telemetry_active) { return; }

    record = newRecord();
    record->recordType = DynamicString;
    record->threadID = subject;
    record->u.annotation_dynamic.intervalID = intervalID;

    /* Dynamic description arbitrarily limited for performance reasons. */
    record->u.annotation_dynamic.description = strndup(description, 1024);
}

void calibrateTSC(FILE *outfile)
{
    unsigned long long startTsc, endTsc;
    uint64_t startTime, endTime;

    startTime = uv_hrtime();
    READ_TSC(startTsc)

    MVM_sleep(1000);

    endTime = uv_hrtime();
    READ_TSC(endTsc)

    {
        unsigned long long ticks = endTsc - startTsc;

        unsigned long long wallClockTime = endTime - startTime;

        ticksPerSecond = (double)ticks / (double)wallClockTime;
        ticksPerSecond *= 1000000000.0;
    }
}

static uv_thread_t backgroundSerializationThread;
static volatile int continueBackgroundSerialization = 1;

void serializeTelemetryBufferRange(FILE *outfile, unsigned int serializationStart, unsigned int serializationEnd)
{
    unsigned int i;
    for(i = serializationStart; i < serializationEnd; i++) {
        struct TelemetryRecord *record = &recordBuffer[i];

        fprintf(outfile, "%10" PRIxPTR " ", record->threadID);

        switch(record->recordType) {
            case Calibration:
                fprintf(outfile, "Calibration: %f ticks per second\n", record->u.calibration.ticksPerSecond);
                break;
            case Epoch:
                fprintf(outfile, "Epoch counter: %lld\n", record->u.epoch.time);
                break;
            case TimeStamp:
                fprintf(outfile, "%15lld -|-  \"%s\"\n", record->u.timeStamp.time - beginningEpoch, record->u.timeStamp.description);
                break;
            case IntervalStart:
                fprintf(outfile, "%15lld (-   \"%s\" (%d)\n", record->u.interval.time - beginningEpoch, record->u.interval.description, record->u.interval.intervalID);
                break;
            case IntervalEnd:
                fprintf(outfile, "%15lld  -)  \"%s\" (%d)\n", record->u.interval.time - beginningEpoch, record->u.interval.description, record->u.interval.intervalID);
                break;
            case IntervalAnnotation:
                fprintf(outfile,  "%15s ???  \"%s\" (%d)\n", " ", record->u.annotation.description, record->u.annotation.intervalID);
                break;
            case DynamicString:
                fprintf(outfile,  "%15s ???  \"%s\" (%d)\n", " ", record->u.annotation_dynamic.description, record->u.annotation_dynamic.intervalID);
                free(record->u.annotation_dynamic.description);
                break;
        }
    }
}

void serializeTelemetryBuffer(FILE *outfile)
{
    unsigned int serializationEnd = recordBufferIndex;
    unsigned int serializationStart = lastSerializedIndex;

    if(serializationEnd < serializationStart) {
        serializeTelemetryBufferRange(outfile, serializationStart, RECORD_BUFFER_SIZE);
        serializeTelemetryBufferRange(outfile, 0, serializationEnd);
    } else {
        serializeTelemetryBufferRange(outfile, serializationStart, serializationEnd);
    }

    lastSerializedIndex = serializationEnd;
}

void backgroundSerialization(void *outfile)
{
    while(continueBackgroundSerialization) {
        MVM_sleep(500);
        serializeTelemetryBuffer((FILE *)outfile);
    }

    fclose((FILE *)outfile);
}

MVM_PUBLIC void MVM_telemetry_init(FILE *outfile)
{
    struct TelemetryRecord *calibrationRecord;
    struct TelemetryRecord *epochRecord;
    int threadCreateError;

    telemetry_active = 1;

    calibrateTSC(outfile);

    calibrationRecord = newRecord();
    calibrationRecord->u.calibration.ticksPerSecond = ticksPerSecond;
    calibrationRecord->recordType = Calibration;

    epochRecord = newRecord();
    READ_TSC(epochRecord->u.epoch.time)
    epochRecord->recordType = Epoch;

    beginningEpoch = epochRecord->u.epoch.time;

    threadCreateError = uv_thread_create((uv_thread_t *)&backgroundSerializationThread, backgroundSerialization, (void *)outfile);
    if (threadCreateError != 0)  {
        telemetry_active = 0;

        fprintf(stderr, "MoarVM: Could not initialize telemetry: %s\n", uv_strerror(threadCreateError));
    }
}

MVM_PUBLIC void MVM_telemetry_finish()
{
    continueBackgroundSerialization = 0;
    uv_thread_join(&backgroundSerializationThread);
}

#else

MVM_PUBLIC void MVM_telemetry_timestamp(MVMThreadContext *threadID, const char *description) { }

MVM_PUBLIC unsigned int MVM_telemetry_interval_start(MVMThreadContext *threadID, const char *description) { return 0; }
MVM_PUBLIC void MVM_telemetry_interval_stop(MVMThreadContext *threadID, int intervalID, const char *description) { }
MVM_PUBLIC void MVM_telemetry_interval_annotate(uintptr_t subject, int intervalID, const char *description) { }
MVM_PUBLIC void MVM_telemetry_interval_annotate_dynamic(uintptr_t subject, int intervalID, char *description) { }

MVM_PUBLIC void MVM_telemetry_init(FILE *outfile) { }
MVM_PUBLIC void MVM_telemetry_finish() { }

#endif
