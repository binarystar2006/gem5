#include "cpu/ctrace.hh"

#include <unistd.h>

#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>

#include "cJSON.h" // Using the cJSON library

ChromeTrace::ChromeTrace(bool debug):debug(debug) {

            // Set server information
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(TRACE_PORT); // Server port
        serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Server IP

        writerThread = std::thread{&ChromeTrace::WriterThread, this};
        receiverThread = std::thread{&ChromeTrace::ReceiverThread, this};
        // receiverThread.join();
        // writerThread.join();
            // Create Socket
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd == -1) {
            perror("Error creating socket");
            exit(1);
        }

        // Connect to server
        while (connect(sockfd, (struct sockaddr*)&serverAddr,
            sizeof(serverAddr)) == -1) {
            perror("Error connecting to server, retrying...");
            sleep(1);
        }
        std::cout<< "ChromeTrace Initiallized Done"<<std::endl;
}

// Function to convert events to JSON string
cJSON* ChromeTrace::EventToJson(const ChromeTraceEvent* event, cJSON *root) {
 // Create cJSON object
    if (event->pid >5 )
        return nullptr;
    cJSON_AddStringToObject(root, "name", event->name);
    cJSON_AddStringToObject(root, "ph", event->ph);
    cJSON_AddNumberToObject(root, "ts", event->ts);
    cJSON_AddStringToObject(root, "pid", processName[event->pid]);
    cJSON_AddStringToObject(root, "tid", threadName[event->tid]);

    if (event->ph[0]=='X') {
        cJSON_AddNumberToObject(root, "dur", event->dur);
    } else if (event->ph[0]=='i') {
        switch (event->scop[0]) {
            case 'g':
            case 'p':
            case 't':
                cJSON_AddStringToObject(root, "s", event->scop);
        }
    }
    if (event->cat)
        cJSON_AddStringToObject(root, "cat", catName[event->cat]);
    return root;
}

// Thread function, used to receive Socket information and save it to std::list
void ChromeTrace::ReceiverThread() {
    long idx=0;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        std::cerr << "Error creating socket" << std::endl;
        return;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(TRACE_PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1)
    {
        std::cerr << "Error binding socket" << std::endl;
        close(sockfd);
        return;
    }

    if (listen(sockfd, 10) == 0) {
        std::cout << "Listening..." << std::endl;
    } else {
        std::cerr << "Error listening" << std::endl;
        close(sockfd);
        return;
    }

    sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    int newSock =
        accept(sockfd, (struct sockaddr*)&clientAddr, &clientAddrLen);

    if (newSock == -1) {
        std::cerr << "Error accepting connection" << std::endl;
        close(sockfd);
        return;
    }

    while (true) {
        ChromeTraceEvent* event = new ChromeTraceEvent();
        ssize_t bytesRead = recv(newSock, event, sizeof(*event), 0);
        if (bytesRead == sizeof(*event)) {
            std::lock_guard<std::mutex> lock(eventMutex);
            if (debug)
                std::cout << "Receiver get "<<idx++<<"s events"<<std::endl;
            eventList.push_back(event);
        }
    }

    close(sockfd);
}

// Thread function for reading packets from std::list and writing to a file
void ChromeTrace::WriterThread() {
    long idx=0;
    std::ofstream outputFile(TRACE_FILE, std::ios::trunc);
    if (!outputFile.is_open()) {
        std::cerr << "Error opening file" << std::endl;
        return;
    }
    outputFile <<"["<<std::endl;
    while (true) {
        // wait for some time
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        std::list<ChromeTraceEvent*> eventsToWrite;

        // std::cout << "Writer thread wait write lock"<<std::endl;
        std::lock_guard<std::mutex> lock(eventMutex);
        // std::cout << "Writer thread get write lock"<<std::endl;
        eventsToWrite = eventList;
        eventList.clear();
        if (debug)
            std::cout << "Writer thread get events :" <<
            eventsToWrite.size() <<std::endl;
        if (!eventsToWrite.empty()) {
            for (const auto& event : eventsToWrite) {
                cJSON *root = cJSON_CreateObject();
                cJSON* jsonEvent = EventToJson(event, root);
                if (jsonEvent == nullptr) {
                    std::cout << "Writer get bad event "<< idx++<<":" <<
                    event->name <<","<< event->tid<<std::endl;
                }
                    // Serialize JSON object to string
                char *jsonStr = cJSON_Print(jsonEvent);
                outputFile << jsonStr <<"," <<std::endl;
                delete event;
                cJSON_Delete(jsonEvent);
            }
        }
    }
    outputFile.close();
}

inline void ChromeTrace::FillCommonEvent(ChromeTraceEvent &event,
                                        uint32_t pid,
                                        uint32_t tid,
                                        const char*name,
                                        uint64_t ts,
                                        uint32_t cat)
{
    memcpy(event.name, name, strlen(name)>63?63:strlen(name));
    event.pid = pid;
    event.tid = tid;
    event.ts = ts;
    event.cat = cat;
}

void ChromeTrace::DurationTraceBegin(uint32_t pid,
                                    uint32_t tid,
                                    const char*name,
                                    uint64_t ts,
                                    uint32_t cat=0)
{
    if (debug)
        std::cout << __func__ <<" pid "<<pid<<", tid "<<
        tid<<", name "<<name<< ", ts "<<ts<<", cat "<<cat<<std::endl;
    ChromeTraceEvent event = {0};
    FillCommonEvent(event, pid, tid, name, ts, cat);
    event.ph[0] = 'B';
    send(sockfd, &event, sizeof(event), 0);
}

void ChromeTrace::DurationTraceEnd(uint32_t pid,
                                    uint32_t tid,
                                    const char*name,
                                    uint64_t ts,
                                    uint32_t cat=0)
{
    if (debug)
        std::cout << __func__ <<" pid "<<pid<<", tid "<<
        tid<<", name "<<name<< ", ts "<<ts<<", cat "<<cat<<std::endl;
    ChromeTraceEvent event={0};
    FillCommonEvent(event, pid, tid, name, ts, cat);
    event.ph[0] = 'E';
    send(sockfd, &event, sizeof(event), 0);
}

void ChromeTrace::CompleteTrace(uint32_t pid,
                                uint32_t tid,
                                const char*name,
                                uint64_t ts,
                                uint64_t dur,
                                uint32_t cat=0)
{
    if (debug)
        std::cout << __func__ <<" pid "<<pid<<", tid "<<tid<<
        ", name "<<name<< ", ts "<<ts<<", dur "<<dur<<", cat "<<cat<<std::endl;
    ChromeTraceEvent event = {0};
    FillCommonEvent(event, pid, tid, name, ts, cat);
    event.ph[0] = 'X';
    event.dur = dur;
    send(sockfd, &event, sizeof(event), 0);
}

void ChromeTrace::InstantTrace(uint32_t pid,
                                uint32_t tid,
                                const char*name,
                                uint64_t ts,
                                char scop,
                                uint32_t cat=0)
{
    if (debug)
        std::cout << __func__ <<" pid "<<pid<<", tid "<<tid<<", name "<<name<<
        ", ts "<<ts<<", scop "<<scop<<", cat "<<cat<<std::endl;
    ChromeTraceEvent event = {0};
    FillCommonEvent(event, pid, tid, name, ts, cat);
    event.ph[0] = 'i';
    event.scop[0] = scop;
    send(sockfd, &event, sizeof(event), 0);
}

void ChromeTrace::CounterTrace(uint32_t pid,
                                uint32_t tid,
                                const char*name,
                                uint64_t ts,
                                uint32_t cat=0)
{
    if (debug)
        std::cout << __func__ <<" pid "<<pid<<", tid "<<tid<<", name "<<name<<
        ", ts "<<ts<<", cat "<<cat<<std::endl;
    ChromeTraceEvent event = {0};
    FillCommonEvent(event, pid, tid, name, ts, cat);
    event.ph[0] = 'C';
    send(sockfd, &event, sizeof(event), 0);
}

void ChromeTrace::AsyncTraceNestStart(uint32_t pid,
                                    uint32_t tid,
                                    const char*name,
                                    uint64_t ts,
                                    uint32_t cat=0) {
    if (debug)
        std::cout << __func__ <<" pid "<<pid<<", tid "<<tid<<", name "<<name<<
        ", ts "<<ts<<", cat "<<cat<<std::endl;
    ChromeTraceEvent event = {0};
    FillCommonEvent(event, pid, tid, name, ts, cat);
    event.ph[0] = 'b';
    send(sockfd, &event, sizeof(event), 0);
}

void ChromeTrace::AsyncTraceNestEnd(uint32_t pid,
                                    uint32_t tid,
                                    const char*name,
                                    uint64_t ts,
                                    uint32_t cat=0)
{
    if (debug)
        std::cout << __func__ <<" pid "<<pid<<", tid "<<tid<<", name "<<name<<
        ", ts "<<ts<<", cat "<<cat<<std::endl;
    ChromeTraceEvent event = {0};
    FillCommonEvent(event, pid, tid, name, ts, cat);
    event.ph[0] = 'e';
    send(sockfd, &event, sizeof(event), 0);
}

void ChromeTrace::AsyncTraceNestInstant(uint32_t pid,
                                        uint32_t tid,
                                        const char*name,
                                        uint64_t ts,
                                        uint32_t cat=0)
{
    if (debug)
        std::cout << __func__ <<" pid "<<pid<<", tid "<<tid<<", name "<<name<<
        ", ts "<<ts<<", cat "<<cat<<std::endl;
    ChromeTraceEvent event = {0};
    FillCommonEvent(event, pid, tid, name, ts, cat);
    event.ph[0] = 'n';
    send(sockfd, &event, sizeof(event), 0);
}
