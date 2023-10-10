# Thread Scheduler

## Design Strategy
Use priority queue to design an algorithm that works for all schedule algorithms.

1. All schedule algorithm will do the jobs below: 
- add a task to ready queue
- remove a task from ready queue as next running task 
- determine preemption condition
- keep the ready queue sorted

The four task are designed as callbacks in this system, init_scheduler will designate proper callbacks to scheduler by given algorithm.

The key difference of these schedule algorithm is how they sort task in ready queue differently. As a result, the callback compare_task may be the most important one. Actually, without MLFQ, the four callbacks can be reduced to two.

Another key feature of this design is that there is no scheduler thread running, task threads will schedule their-selves. As a result, in schedule_me function, the top half of this function if not about the task itself, but about determine next running task, then it wait on a conditional variable to do its own job (get cpu burst time)

2. How broke it up pieces

This design has separated the framework code and code about algorithm.

We first finished the framework and managed to get FCFS work, then SRJF and PBS is just about different strategy to compare task. We put the MLFQ last, since it is totally different.
