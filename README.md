
# runwatch

This utility helps to execute a command on a Linux system under (RSS) memory, time, and CPU constraints, for example for performance evaluations or for basic scheduling of tasks on a parallel machine.

It is similar to [pshved/timeout](https://github.com/pshved/timeout) in that you can set run time and memory limits for a given command. 
However, runwatch has the ability to pin the execution of each command to a user-provided number of cores and can also measure memory of child processes.
Runwatch can also schedule multiple "homogeneous" commands (same resources, time and memory limits) in a simple fashion.
In turn, runwatch requires **two free cores** of its own for scheduling and for frequent memory and time limit checking.

## Requirements

GCC compiler. Run `bash compile.sh` to build.

## Usage

```
./runwatch <tasks_file> [-p|-np|--processes <num_parallel_processes>] [-t|--threads-per-process <num_threads_per_process>] [-T|--timelim <timelimit_seconds>] [-M|--memlim <rss_memlimit_kilobytes>] [-d|--directory <output_log_directory>] [-r|--recurse-children] [-q|--quiet]
```

Assume you have eight physical cores and each command you execute runs two parallel threads. You would use the parameters `-np 3 -t 2` to have 3*2=6 "worker" threads running the commands and to have the remaining two cores left for runwatch.

Each line in <tasks_file> must begin with a unique instance id (e.g. the current line number) followed by a whitespace and then the command to execute.

For each command, a line of this form is printed to stdout:

```
{job id} RUNWATCH_RESULT {EXIT|TIMEOUT|MEMOUT} RETVAL {return value} TIME_SECS {wallclock time elapsed} MEMPEAK_KBS {memory peak}
```

### Example

The text file `commands` contains an example list of commands. The first argument to the program `test` is the number of threads and the second argument is the time limit (or no argument for running indefinitely).

Run `bash compile_test.sh` and then try out the program with this command:

`./runwatch commands -p 3 -t 2 -T 2 -M 10000 -d log/|grep RESULT`

This limits each command to two seconds of wallclock time and to 10MB of memory.
On my machine, this leads to something like this:

```
3 RUNWATCH_RESULT EXIT RETVAL 0 TIME_SECS 1.00386 MEMPEAK_KBS 7308
4 RUNWATCH_RESULT EXIT RETVAL 0 TIME_SECS 0.0128961 MEMPEAK_KBS 224
5 RUNWATCH_RESULT EXIT RETVAL 0 TIME_SECS 0.00407314 MEMPEAK_KBS 224
1 RUNWATCH_RESULT TIMEOUT RETVAL 0 TIME_SECS 2.0015 MEMPEAK_KBS 5396
2 RUNWATCH_RESULT TIMEOUT RETVAL 0 TIME_SECS 2.00136 MEMPEAK_KBS 7328
6 RUNWATCH_RESULT MEMOUT RETVAL 0 TIME_SECS 1.00076 MEMPEAK_KBS 32500
9 RUNWATCH_RESULT MEMOUT RETVAL 0 TIME_SECS 1.00076 MEMPEAK_KBS 25128
10 RUNWATCH_RESULT EXIT RETVAL 0 TIME_SECS 0.102789 MEMPEAK_KBS 224
7 RUNWATCH_RESULT TIMEOUT RETVAL 0 TIME_SECS 2.00103 MEMPEAK_KBS 5296
8 RUNWATCH_RESULT TIMEOUT RETVAL 0 TIME_SECS 2.0011 MEMPEAK_KBS 7168
```

In addition, the output of the respective commands can be found in the directories `log/<job-id>/`.

## Caveats

Note that `runwatch` uses `SIGINT` (interrupt signal) for gracefully stopping the command when hitting a (time|mem)out, so it is important that the program handles this signal properly. (Otherwise, if the program does not react to the signal, after a certain timeout `SIGKILL` will be used.)

Very short runtimes (of few milliseconds) may be reported inaccurately. The runtime of a program displayed in the result line may differ slightly from the program's actual run time. On a not too crowded system this effect should be in terms of few milliseconds.

runwatch has no power over what its child processes are doing, in particular the executed program could just re-map the CPUs it may compute on to something else. So make sure that the program does not internally call something like `sched_setaffinity` itself when using it with CPU pinning.
