
#include <cstdlib>
#include <iostream>
#include <chrono>
#include <thread>
#include <string>
#include <vector>
#include <signal.h>

bool exiting = false;

void handler(int signum) {
    exiting = true;
}

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

int main(int argc, const char** argv) {
    using namespace std::chrono_literals;
    
    Timer::init();
    
    signal(SIGINT, handler);
    signal(SIGTERM, handler);
    
    size_t numThreads = 1;
    if (argc > 1) {
        numThreads = atoi(argv[1]);
    }
    float timelim = -1;
    if (argc > 2) {
        timelim = atof(argv[2]);
    }
    std::cout << numThreads << " threads, time limit: " << timelim << "s" << std::endl;
    
    auto execute = [&, timelim]() {
        std::vector<int> numbers;
        while (!exiting && (timelim < 0 || Timer::elapsedSeconds() < timelim)) {
            int num = 0;
            for (int i = numbers.size()-10; i < (int)numbers.size(); i++) {
                if (i >= 0) num += numbers[i];
            }
            numbers.push_back(num);
        }
    };
    
    std::vector<std::thread> threads;
    for (size_t i = 1; i < numThreads; i++) {
        threads.emplace_back(execute);
    }
    execute();
    
    for (auto& thread : threads) thread.join();
    std::cout << "Program exiting." << std::endl;
}
