# runwatch

This utility helps to execute a command on a Linux system under memory, time, and CPU constraints, for example for performance evaluations or for basic scheduling of tasks on a parallel machine.

It is similar to [pshved/timeout](https://github.com/pshved/timeout) in that you can set run time and memory limits for a given command. 
However, runwatch has the ability to pin the execution of a command to a user-provided set of CPUs and measures memory based on [Proportional Set Size (PSS)](https://en.wikipedia.org/wiki/Proportional_set_size) which is more accurate than Residual Set Size (RSS) in cases where the executed command has child (or grand-child ...) processes with some memory shared among them.
In turn, runwatch is slower, hence it should be executed on a separate, otherwise idle CPU (you can do that with a simple program option).

Also, runwatch should play nicely with GNU parallel when you map different executions to different CPU sets.

## Requirements

Python 3 and the `psutil` package (`pip install psutil`).

## Usage

```
./runwatch [-h|--help] [-v|--verbose] [<timelim>] [<memlim>] [cpu<i>..<j> | cpu<i>:<j> | cpu<i>[ cpu<j>[ ...]]] [wcpu<i>] <command>
```

For example, `cpu0 cpu1 cpu2` is equivalent to `cpu0:2` or `cpu0..2` and pins <command> to CPUs #0 through #2.  
Use `wcpu{i}` ("watching CPU") to pin the watching parent process to CPU #`{i}`.

Formatting of run time limits: {number}{unit} where {unit} may be ms, s, min, or h (case insensitive).  
Formatting of memory limits: {number}{unit} where {unit} may be b, kb, mb, gb, or tb (case insensitive).

The output of the program is left untouched with the exception of a single line with eight whitespace-separated words printed to stdout right before exiting:

```
RUNWATCH_RESULT {EXIT|TIMEOUT|MEMOUT} RETVAL {return value} TIME_SECS {wallclock time elapsed} MEMPEAK_KBS {memory peak}
```
(Also, you obviously get additional output when using the `-v` flag.)

### Example

`./runwatch 5min 8gb cpu0:3 wcpu4 ./mallob -mono=f.cnf`

This command limits the execution of `./mallob -mono=f.cnf` to five minutes of wallclock time and 8GiB of (effective) RAM and pins it to CPUs #0 through #3.
CPU #4 is used for monitoring.

## Caveats

Note that `runwatch` uses `SIGINT` (interrupt signal) for gracefully stopping the command when hitting a (time|mem)out, so it is important that the program handles this signal properly. (Otherwise, if the program does not react to the signal, after a certain timeout `SIGKILL` will be used.)

Very short runtimes (of a few milliseconds) may be handled and reported inaccurately. The runtime of a program displayed in the result line may differ slightly from the program's actual run time. On a not too crowded system this effect should be in terms of a couple milliseconds.

runwatch has no true power over what its child processes are doing, in particular the executed program could just re-map the CPUs it may compute on to something else. So make sure that the program does not internally call something like `sched_setaffinity` itself when using it with CPU pinning.
