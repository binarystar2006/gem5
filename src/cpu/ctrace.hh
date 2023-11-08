#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>

#include <list>
#include <map>
#include <mutex>
#include <thread>

#define TRACE_FILE "event.json" //定义trace文件名
#define TRACE_PORT 12345

class cJSON;

const char catName[][128] {
    {""},{"Squash"},{"second"},{"third"}
};

const char processName[][128] {
    {"Thread0"},{"Thread1"},{"Thread2"}
};

const char threadName[][128] {
    {"fetch"},{"decode"},{"rename"},{"iew"},{"commit"}
};
struct ChromeTraceEvent
{
    char name[64] = {0};
    char ph[2] = "";
    uint64_t ts = 0;
    uint64_t dur = 0;
    char scop[2] = "";
    int pid = 0;
    int tid = 0;
    uint32_t cat = 0;
};
class ChromeTrace
{
public:
    ChromeTrace(bool debug);
    ~ChromeTrace() {
        // Close Socket connection
        if (sockfd)
            close(sockfd);
    }
    int GetSockfd() {
        return sockfd;
    }

    void DurationTraceBegin(uint32_t pid,
                            uint32_t tid,
                            const char*name,
                            uint64_t ts,
                            uint32_t cat);

    void DurationTraceEnd(uint32_t pid,
                        uint32_t tid,
                        const char*name,
                        uint64_t ts,
                        uint32_t cat);

    void CompleteTrace(uint32_t pid,
                        uint32_t tid,
                        const char*name,
                        uint64_t ts,
                        uint64_t dur,
                        uint32_t cat);

    void InstantTrace(uint32_t pid,
                    uint32_t tid,
                    const char*name,
                    uint64_t ts,
                    char scop,
                    uint32_t cat);

    void CounterTrace(uint32_t pid,
                    uint32_t tid,
                    const char*name,
                    uint64_t ts,
                    uint32_t cat);

    void AsyncTraceNestStart(uint32_t pid,
                            uint32_t tid,
                            const char*name,
                            uint64_t ts,
                            uint32_t cat);

    void AsyncTraceNestEnd(uint32_t pid,
                            uint32_t tid,
                            const char*name,
                            uint64_t ts,
                            uint32_t cat);

    void AsyncTraceNestInstant(uint32_t pid,
                                uint32_t tid,
                                const char*name,
                                uint64_t ts,
                                uint32_t cat);

    void FlowTraceStart();
    void FlowTraceEnd();
    void FlowTraceStep();
    void SampleTrace();
    void ObjectTraceCreate();
    void ObjectTraceDestroy();
    void ObjectTraceSnapshot();
    void MetadataTrace();
    void MemoryDumpTraceGlobal();
    void MemoryDumpTraceProcess();
    void MarkTrace();
    void ClockSyncTrace();

private:
    inline void FillCommonEvent(ChromeTraceEvent &event,
                                uint32_t pid,
                                uint32_t tid,
                                const char*name,
                                uint64_t ts,
                                uint32_t cat);

    cJSON* EventToJson(const ChromeTraceEvent* event, cJSON *root);
    void ReceiverThread();
    void WriterThread();

    std::map<uint32_t, const char*> trcNameMap;
    std::list<ChromeTraceEvent*> eventList; // Use std::list to store events
    /*Mutex lock for mutually exclusive access to event list*/
    std::mutex eventMutex;
    /*receiver*/
    std::thread receiverThread;
    std::thread writerThread;
    /*sender*/
    struct sockaddr_in serverAddr;
    int sockfd;
    /*common*/
    bool debug;
};
