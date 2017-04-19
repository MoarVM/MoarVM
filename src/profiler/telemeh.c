#include <moar.h>

#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

#ifdef _WIN32
#include <intrin.h>
#else
#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#else
#define __rdtscp(V) { V = 0; }
#endif
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
    };
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

void MVM_telemetry_timestamp(MVMThreadContext *threadID, const char *description)
{
    struct TelemetryRecord *record;

    if (!telemetry_active) { return; }

    record = newRecord();

    READ_TSC(record->timeStamp.time);
    record->recordType = TimeStamp;
    record->threadID = (uintptr_t)threadID;
    record->timeStamp.description = description;
}

unsigned int MVM_telemetry_interval_start(MVMThreadContext *threadID, const char *description)
{
    struct TelemetryRecord *record;

    unsigned int intervalID;

    if (!telemetry_active) { return 0; }

    record = newRecord();
    MVM_incr(&intervalIDCounter);
    intervalID = MVM_load(&intervalIDCounter);
    READ_TSC(record->interval.time);

    record->recordType = IntervalStart;
    record->threadID = (uintptr_t)threadID;
    record->interval.intervalID = intervalID;
    record->interval.description = description;

    return intervalID;
}

void MVM_telemetry_interval_stop(MVMThreadContext *threadID, int intervalID, const char *description)
{
    struct TelemetryRecord *record;

    if (!telemetry_active) { return; }

    record = newRecord();
    READ_TSC(record->interval.time);

    record->recordType = IntervalEnd;
    record->threadID = (uintptr_t)threadID;
    record->interval.intervalID = intervalID;
    record->interval.description = description;
}

void MVM_telemetry_interval_annotate(uintptr_t subject, int intervalID, const char *description) {
    struct TelemetryRecord *record;

    if (!telemetry_active) { return; }

    record = newRecord();
    record->recordType = IntervalAnnotation;
    record->threadID = subject;
    record->annotation.intervalID = intervalID;
    record->annotation.description = description;
}

void MVM_telemetry_interval_annotate_dynamic(uintptr_t subject, int intervalID, char *description) {
    struct TelemetryRecord *record;
    char *temp;

    if (!telemetry_active) { return; }

    temp = malloc(strlen(description) + 1);
    strncpy(temp, description, strlen(description) + 1);

    record = newRecord();
    record->recordType = DynamicString;
    record->threadID = subject;
    record->annotation_dynamic.intervalID = intervalID;
    record->annotation_dynamic.description = temp;
}

void calibrateTSC(FILE *outfile)
{
    unsigned long long startTsc, endTsc;
    uint64_t startTime, endTime;

    startTime = uv_hrtime();
    READ_TSC(startTsc)

    sleep(1);

    endTime = uv_hrtime();
    READ_TSC(endTsc)

    {
        unsigned long long ticks = endTsc - startTsc;

        unsigned long long wallClockTime = endTime - startTime;

        ticksPerSecond = (double)ticks / (double)wallClockTime;
        ticksPerSecond *= 1000000000.0;
    }
}

static pthread_t backgroundSerializationThread;
static volatile int continueBackgroundSerialization = 1;

void serializeTelemetryBufferRange(FILE *outfile, unsigned int serializationStart, unsigned int serializationEnd)
{
    unsigned int i;
    for(i = serializationStart; i < serializationEnd; i++) {
        struct TelemetryRecord *record = &recordBuffer[i];

        fprintf(outfile, "%10" PRIxPTR " ", record->threadID);

        switch(record->recordType) {
            case Calibration:
                fprintf(outfile, "Calibration: %f ticks per second\n", record->calibration.ticksPerSecond);
                break;
            case Epoch:
                fprintf(outfile, "Epoch counter: %lld\n", record->epoch.time);
                break;
            case TimeStamp:
                fprintf(outfile, "%15lld -|-  \"%s\"\n", record->timeStamp.time - beginningEpoch, record->timeStamp.description);
                break;
            case IntervalStart:
                fprintf(outfile, "%15lld (-   \"%s\" (%d)\n", record->interval.time - beginningEpoch, record->interval.description, record->interval.intervalID);
                break;
            case IntervalEnd:
                fprintf(outfile, "%15lld  -)  \"%s\" (%d)\n", record->interval.time - beginningEpoch, record->interval.description, record->interval.intervalID);
                break;
            case IntervalAnnotation:
                fprintf(outfile,  "%15s ???  \"%s\" (%d)\n", " ", record->annotation.description, record->annotation.intervalID);
                break;
            case DynamicString:
                fprintf(outfile,  "%15s ???  \"%s\" (%d)\n", " ", record->annotation_dynamic.description, record->annotation_dynamic.intervalID);
                free(record->annotation_dynamic.description);
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

void *backgroundSerialization(void *outfile)
{
    while(continueBackgroundSerialization) {
        sleep(1);
        serializeTelemetryBuffer((FILE *)outfile);
    }

    fclose((FILE *)outfile);

    return NULL;
}

void MVM_telemetry_init(FILE *outfile)
{
    struct TelemetryRecord *calibrationRecord;
    struct TelemetryRecord *epochRecord;

    telemetry_active = 1;

    calibrateTSC(outfile);

    calibrationRecord = newRecord();
    calibrationRecord->calibration.ticksPerSecond = ticksPerSecond;
    calibrationRecord->recordType = Calibration;

    epochRecord = newRecord();
    READ_TSC(epochRecord->epoch.time)
    epochRecord->recordType = Epoch;

    beginningEpoch = epochRecord->epoch.time;

    pthread_create(&backgroundSerializationThread, NULL, backgroundSerialization, (void *)outfile);
}

void MVM_telemetry_finish()
{
    continueBackgroundSerialization = 0;
    pthread_join(backgroundSerializationThread, NULL);
}
