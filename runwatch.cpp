
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <string>
#include <vector>
#include <chrono>
#include <sstream>
#include <fstream>
#include <list>
#include <iostream>
#include <cmath>
#include <atomic>
#include <thread>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

using namespace std::chrono;

class Timer {

private:
    static double startTime;

public:

    static void init(double start = -1) {
        startTime = start == -1 ? now() : start;
    }

    static double now() {
        return 0.001 * 0.001 * 0.001 * system_clock::now().time_since_epoch().count();
    }

    /**
     * Returns elapsed time since program start (since MyMpi::init) in seconds.
     */
    static float elapsedSeconds() {
        return now() - startTime;
    }
};

double Timer::startTime;

int mkdirP(const std::string& dir) {
    for (size_t i = 0; i < dir.size(); i++) {
        if (dir[i] == '/' && i > 0 && i+1 < dir.size()) {
            std::string subdir(dir.begin(), dir.begin() + i);
            int res = ::mkdir(subdir.c_str(), S_IRWXU | S_IRWXG | S_IRWXO);   
            if (res != 0 && errno != EEXIST) {
                return res;
            }  
        }
    }
    auto res = ::mkdir(dir.c_str(), S_IRWXU | S_IRWXG | S_IRWXO);
    if (res == 0 || errno == EEXIST) return 0;
    return res;
}

void pinProcessToCpu(pid_t pid, int firstCpu, size_t numCpus) {
	cpu_set_t cpuSet;
	CPU_ZERO(&cpuSet);
    for (size_t i = 0; i < numCpus; i++) {
	    CPU_SET(firstCpu+i, &cpuSet);
    }
	sched_setaffinity(pid, sizeof(cpuSet), &cpuSet);
}

long getResidentSetSize(pid_t pid, bool recurse) {

    using std::ios_base;
    using std::ifstream;
    using std::string;

    auto statFile = "/proc/" + std::to_string(pid) + "/stat";
    ifstream stat_stream(statFile.c_str(), ios_base::in);
    if (!stat_stream.good()) return 0;

    // dummy vars for leading entries in stat that we don't care about
    string str_pid, comm, state, ppid, pgrp, session, tty_nr;
    string tpgid, flags, minflt, cminflt, majflt, cmajflt;
    string utime, stime, cutime, cstime, priority, nice;
    string O, itrealvalue, starttime;

    // the two fields we want
    unsigned long vsize = 0;
    long rss = 0;

    stat_stream >> str_pid >> comm >> state >> ppid >> pgrp >> session >> tty_nr
                >> tpgid >> flags >> minflt >> cminflt >> majflt >> cmajflt
                >> utime >> stime >> cutime >> cstime >> priority >> nice
                >> O >> itrealvalue >> starttime >> vsize >> rss; // don't care about the rest

    stat_stream.close();

    long page_size_kb = sysconf(_SC_PAGE_SIZE) / 1024; // in case x86-64 is configured to use 2MB pages
    rss *= page_size_kb;
    
    // Recursively read memory usage of all children
    if (recurse) {
        auto childFile = "/proc/" + std::to_string(pid) + "/task/" + std::to_string(pid) + "/children";
        ifstream children_stream(childFile.c_str(), ios_base::in);
        if (!children_stream.good()) return rss;

        pid_t childPid;
        while (children_stream >> childPid) {
            rss += getResidentSetSize(childPid, recurse);
        }
    }

    return rss;
}

struct Process {

    int instanceId = -1;
    pid_t pid = -1;
    
    float starttime = 0;
    float lastLimitCheck = -1;
    bool running = false;
    size_t koCounter = 0;

    int status = 0;
    int retval = 0;
    float runtimeSecs = 0;
    long mempeakKbs = 0;
    
    std::vector<std::string> command;
};

bool allExiting = false;

void handler(int signum) {
    allExiting = true;
}

const int STATUS_TIMEOUT = 1;
const int STATUS_MEMOUT = 2;
const int STATUS_RUNNING = 3;

int main(int argc, const char** argv) {

    Timer::init();
    
    signal(SIGINT, handler);
    signal(SIGTERM, handler);

    size_t numProcesses = 1;
    size_t threadsPerProcess = 1;
    float timelimSecs = 0;
    long memlimKbs = 0;
    std::string commandsFilename;
    std::string outputDir = ".";
    bool recurse = false;
    bool quiet = false;

    // Parse args
    for (size_t i = 1; i < (size_t)argc; i++) {
        std::string arg(argv[i]);
        if (arg == "-p" || arg == "-np" || arg == "--processes") {
            arg = argv[++i];
            numProcesses = atoi(arg.c_str());
        } else if (arg == "-t" || arg == "--threads-per-process") {
            arg = argv[++i];
            threadsPerProcess = atoi(arg.c_str());
        } else if (arg == "-T" || arg == "--timelim") {
            arg = argv[++i];
            timelimSecs = atof(arg.c_str());
        } else if (arg == "-M" || arg == "--memlim") {
            arg = argv[++i];
            memlimKbs = atoi(arg.c_str());
        } else if (arg == "-d" || arg == "--directory") {
            arg = argv[++i];
            outputDir = arg;
        } else if (arg == "-r" || arg == "--recurse-children") {
            recurse = true;
        } else if (arg == "-q" || arg == "--quiet") {
            quiet = true;
        } else if (arg[0] != '-') {
            // Positional arg: Filename with commands
            commandsFilename = arg;
        }
    }

    if (commandsFilename.empty()) {
        std::cout << "Usage: runwatch <tasks_file>"
            << " [-p|-np|--processes <num_parallel_processes>]"
            << " [-t|--threads-per-process <num_threads_per_process>]"
            << " [-T|--timelim <timelimit_seconds>]"
            << " [-M|--memlim <rss_memlimit_kilobytes>]"
            << " [-d|--directory <output_log_directory>]"
            << " [-r|--recurse-children]"
            << " [-q|--quiet]" << std::endl;
        std::cout << "Each line in <tasks_file> must begin with a unique instance id "
            << "(e.g. the current line number) followed by a whitespace and then "
            << "the command to execute." << std::endl;
        return 0;
    }

    // Pin this process to the slot directly following all worker task slots
    pinProcessToCpu(0, /*firstCpu=*/threadsPerProcess*numProcesses, /*numCpus=*/2);

    std::vector<Process> activeChildren;
    activeChildren.resize(numProcesses);

    // Read tasks
    std::list<Process> tasks;
    std::ifstream infile(commandsFilename);
    std::string line;
    size_t numDoneTasks = 0;
    size_t numTasks = 0;
    while (std::getline(infile, line)) {
        std::istringstream iss(line);
        int instanceId;
        if (!(iss >> instanceId)) { break; } // error
        tasks.emplace_back();
        tasks.back().instanceId = instanceId;
        std::string arg;
        while (iss >> arg) {
            tasks.back().command.push_back(arg);
        }
        numTasks++;
    }

    // Recognize finished children in a separate thread
    std::thread childExitListener = std::thread([&]() {
        int status = 0;
        while (!allExiting) {
            pid_t finishedPid = waitpid(-1, &status, WNOHANG);
            if (finishedPid > 0) {
                // Some child finished
                float endtime = Timer::elapsedSeconds();
                int i = 0;
                while (!activeChildren[i].running || activeChildren[i].pid != finishedPid) i++;
                assert(i >= 0 && i < (int)activeChildren.size());
                auto& child = activeChildren[i];
                child.runtimeSecs = endtime - child.starttime;
                child.retval = status;
                child.running = false;
            }
        }
    });

    // Main loop
    bool anyActive = false;
    while (anyActive || !tasks.empty()) {
        anyActive = false;
        
        // For each task slot
        for (size_t i = 0; i < activeChildren.size(); i++) {

            // Is there an active process at this slot?
            if (activeChildren[i].pid > 0) {
                auto& child = activeChildren[i];
                anyActive = true;

                // Did child terminate?
                if (!activeChildren[i].running) {
                    
                    // Create runwatch report line
                    std::string statusStr = child.status == STATUS_MEMOUT ? "MEMOUT" : 
                                    child.status == STATUS_TIMEOUT ? "TIMEOUT" : 
                                    "EXIT";
                    std::stringstream ss;
                    ss << "RUNWATCH_RESULT " << statusStr 
                            << " RETVAL " << child.retval << " TIME_SECS " << child.runtimeSecs 
                            << " MEMPEAK_KBS " << child.mempeakKbs << std::endl;
                    
                    // Write report to stdout
                    if (!quiet) std::cout << child.instanceId << " " << ss.str();
                    // Write report to the instance's log file
                    std::string instOutfile = outputDir + "/" + std::to_string(activeChildren[i].instanceId) + "/rw";
                    std::ofstream ofs(instOutfile, std::ios_base::app);
                    ofs << std::endl << ss.str();
                    ofs.close();

                    activeChildren[i].pid = -1;
                    numDoneTasks++;
                    
                    if (!quiet) std::cout << child.instanceId << " END (" 
                            << numDoneTasks << "/" << numTasks << " done)" << std::endl;
                
                } else {
                    // Active child
                    float time = Timer::elapsedSeconds();
                    auto& child = activeChildren[i];

                    if (allExiting) {
                        // Forward interruption to child
                        kill(child.pid, SIGINT);
                        continue;
                    }
                    
                    if (time - child.lastLimitCheck >= 1.0f) {
                        // Limit check
                        child.lastLimitCheck = time;

                        if (timelimSecs > 0 && time - child.starttime > timelimSecs) {
                            // Timeout
                            kill(child.pid, SIGINT);
                            child.status = STATUS_TIMEOUT;
                            child.koCounter++;

                        } else {
                            // Check memory limit, update mempeak
                            long rssKbs = getResidentSetSize(child.pid, recurse);
                            child.mempeakKbs = std::max(child.mempeakKbs, rssKbs);
                            if (memlimKbs > 0 && child.mempeakKbs > memlimKbs) {
                                // Memout
                                kill(child.pid, SIGINT);
                                child.status = STATUS_MEMOUT;
                                child.koCounter++;
                            }
                        }

                        // Hardkill if unresponsive
                        if (child.koCounter >= 5) kill(child.pid, SIGKILL);
                    }
                }
            }
            if (allExiting) continue;
            
            // Is this slot currently empty and there are still tasks left?
            if (activeChildren[i].pid < 0 && !tasks.empty()) {
                
                // (Re)allocate for next task
                activeChildren[i] = tasks.front();
                tasks.pop_front();
                
                // Assemble command for execvp
                if (!quiet) std::cout << activeChildren[i].instanceId << " BEGIN" << std::endl;
                char* const* argv = [&]() {
                    const char** argv = new const char*[activeChildren[i].command.size()+1];
                    int j = 0;
                    for (const auto& arg : activeChildren[i].command) {
                        argv[j] = arg.c_str();
                        j++;
                    }
                    argv[j] = nullptr;
                    return (char* const*) argv;
                }();

                // Create log directory
                std::string instanceDir = outputDir + "/" + std::to_string(activeChildren[i].instanceId) + "/";
                int res = mkdirP(instanceDir.c_str());
                if (res != 0) {
                    if (!quiet) std::cout << activeChildren[i].instanceId 
                        << " ERROR: Cannot create directory \"" << instanceDir << "\" - skipping"
                        << std::endl;
                } else {
                    
                    // Start the subprocess
                    pid_t pid = fork();
                    if (pid == 0) {
                        // Child:
                        // Redirect stdout and stderr to log file
                        int newStdout = open((instanceDir + "rw").c_str(), O_CREAT |  O_WRONLY, S_IRWXU);
                        dup2(newStdout, 1); // stdout
                        dup2(newStdout, 2); // stderr
                        // Execute command
                        execvp(argv[0], argv);
                    }
                    
                    // Parent
                    activeChildren[i].pid = pid;
                    pinProcessToCpu(pid, threadsPerProcess * i, threadsPerProcess);
                    activeChildren[i].starttime = Timer::elapsedSeconds();
                    activeChildren[i].status = STATUS_RUNNING;
                    activeChildren[i].running = true;

                    // Clean up
                    delete[] argv;
                    anyActive = true;
                }

            }
        }
        
        if (allExiting) break;
    }

    allExiting = true;
    childExitListener.join();
}
