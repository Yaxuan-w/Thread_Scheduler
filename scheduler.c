/**
 * 
 * File             : scheduler.c
 * Description      : This is a stub to implement all your scheduling schemes
 *
 * Author(s)        : @Yaxuan Wen @Zixin Li
 * Last Modified    : @10/12/2020
*/

// Include Files
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <time.h>

#include <math.h>
#include <pthread.h>

void init_scheduler( int sched_type );
int schedule_me( float currentTime, int tid, int remainingTime, int tprio );
int P( float currentTime, int tid, int sem_id);
int V( float currentTime, int tid, int sem_id);

#define FCFS    0
#define SRTF    1
#define PBS     2
#define MLFQ    3
#define SEM_ID_MAX 50



/*
 * doubly linked list definition and implementation
 * act as ready queue and wait queue in this project
 */

// list node def
typedef struct list_node {
  struct list_node* prev;
  struct list_node* next;
  void* data;
} list_node_t;

// list def
typedef struct list {
  list_node_t head_guard; // head guard, pseudo head
  list_node_t tail_guard;  // tail guard, pseudo tail
} list_t;


// list initialization
void list_init(list_t* list) {
    list->head_guard.next = &(list->tail_guard);
    list->tail_guard.prev = &(list->head_guard);
    list->head_guard.prev = NULL;
    list->tail_guard.next = NULL;
}

// create new node
// linked to list
list_node_t* list_new_node(void* data) {
    list_node_t* new_node = calloc(1, sizeof(list_node_t));
    new_node->data = data;
    return new_node;
}

// check if list is empty
int list_empty(list_t* list) {
    return (list->head_guard.next == &list->tail_guard);
}


// add element to list tail
list_node_t* list_append(list_t* list, void* data) {
    list_node_t* new_node = list_new_node(data);
    new_node->next = &list->tail_guard;
    new_node->prev = list->tail_guard.prev;
    new_node->prev->next = new_node;
    new_node->next->prev = new_node;
}

// list element comparator
// to make list in order
typedef int(* cmp_t)(const void* , const void*);


// insert element to list in order
// defined by comparator
list_node_t* list_insert(list_t* list, void* data, cmp_t compare) {
    if (!compare) {
        return list_append(list, data);
    }
    list_node_t* p = list->head_guard.next;
    while (p != &(list->tail_guard) && compare(data, (p)->data) > 0){
        p = p->next;
    }
    list_node_t* new_node = list_new_node(data);
    new_node->next = p;
    new_node->prev = p->prev;
    new_node->prev->next = new_node;
    new_node->next->prev = new_node;
    return new_node;
}


// remove node from list
// since it is doubly linked
// remove is very fast
void* list_remove(list_node_t* node) {
    void* ret = node->data;
    node->next->prev = node->prev;
    node->prev->next = node->next;
    free(node);
    return ret;
}

// remove first element and return the data
void* list_popfront(list_t* list) {
    if (list_empty(list)) return NULL;
    void* ret = list_remove(list->head_guard.next);
    return ret;
}


/*
 * task definition, needed by scheduler
 */
typedef struct task {
  struct task* next; // all tasks form a singly linked list
  int tid;       // tid
  float arrival;  // arrival time
  int cur_tick;   // current system tick
  int tprio;      // priority
  int remain;     // remain burst time
  int bursted;    // bursted in this round
  int level;      // the ready queue level, only used in mlfq
  pthread_cond_t cond;  // conditional variable when it is waiting for running
} task_t;

typedef task_t* task_list_t;

// compare task by arrival
// used by fcfs, and call from waiting queue
int compare_task_by_arrival(const void* t1, const void* t2) {
    const task_t* task1 = t1;
    const task_t* task2 = t2;
    if (task1->arrival < task2->arrival) {
        return -1;
    }
    if (task1->arrival == task2->arrival) {
        return 0;
    }
    return 1;
}

// create new task
task_t* task_new(int tid,int tprio) {
    task_t* new_task = malloc(sizeof(task_t));
    new_task->tid = tid;
    new_task->tprio = tprio;
    new_task->cur_tick = -1;
    new_task->remain = -1;
    new_task->level = -1;
    pthread_cond_init(&new_task->cond, NULL);
    new_task->next = NULL;
    return new_task;
}

// add task to task list
task_t* task_add(task_list_t* plist, int tid,int tprio) {
    task_t* new_task = task_new(tid,tprio);
    new_task->next = *plist;
    *plist = new_task;
    return new_task;
}

// find task by tid
task_t* task_find(task_list_t list, int tid) {
    for (task_t* p = list; p; p = p->next) {
        if (p->tid == tid) {
            return p;
        }
    }
    return NULL;
}

// remove task by tid
void task_remove(task_list_t* plist, int tid) {
    for (task_t** pp = plist; *pp; pp = &((*pp)->next)) {
        if ((*pp)->tid == tid) {
            task_t* old = (*pp);
            (*pp) = (*pp)->next;
            pthread_cond_destroy(&old->cond);
            free(old);
            return;
        }
    }
}


// for debug
void print_queue(list_t* list) {
    //printf("queue is: ");
    for (list_node_t* p = list->head_guard.next; p != &(list->tail_guard); p = p->next) {
        //printf("task %d, remaining %d", ((task_t*)p->data)->tid, ((task_t*)p->data)->remain);
    }
    //printf("\n");
}


/*
 * scheduler callbacks definitions
 */

// check if task should yield
typedef int(* should_yield_t)(task_t* task) ;

// what to do on every tick
typedef void(* on_tick_t)() ;

// add task to ready queue
typedef void(* task_ready_t)(task_t*) ;

// pop a task from ready queue
typedef task_t*(*task_next_t)();

/*
 * task scheduler
 */
typedef struct scheduler {
  should_yield_t should_yield;
  cmp_t compare_task;
  on_tick_t on_tick; // what to do on every tick..
  task_ready_t task_ready;
  task_next_t next_task;


} scheduler_t;

pthread_mutex_t lock_scheduler; // lock to protect all the data
task_t* all_tasks; // all thread in system, to lookup priority and cond variable
list_t waiting[SEM_ID_MAX]; // blocking queues
int sem_values[SEM_ID_MAX]; // sem values of each semaphore


list_t ready;  // ready queue
list_t mlfq_ready[5]; // ready queues for mlfq
task_t* running; // current running task
task_t* waken_up; // task that just waken up
pthread_cond_t cond_waken_up;
int cur_tick;    // global clock
scheduler_t scheduler; // scheduler specific data

/*
 * update global time to ceil(currentTime)
 * if currentTime is advanced to global tick
 */
void align_tick(float currentTime) {
    if (cur_tick * 1.0 < currentTime) {
        cur_tick = (int)(ceil((double)currentTime));
    }
}

/*
 * next task call back for algorithm except for mlfq
 */
task_t* next_task_common() {
    task_t* next = list_popfront(&ready);
    if (next) {
        next->bursted = 0;
    }
    return next;
}

/*
 * add task to ready queue call back for algorithm except for mlfq
 */
void task_ready_common(task_t* task) {
    list_insert(&ready, task, scheduler.compare_task);
}



/*
 * scheduler callbacks
 */
void fcfs_tick() {
    // nothing to do
}

int fcfs_should_yield(task_t* task) {
    (void)task;
    return 0; // non preepmtive
}

void pbs_tick() {
    // nonthing to do
}

int compare_by_current_time(const void* t1, const void* t2) {
    const task_t* task1 = t1;
    const task_t* task2 = t2;
    if (task1->cur_tick < task2->cur_tick) {
        return -1;
    } else if (task1->cur_tick < task2->cur_tick) {
        return 0;
    }
    return 1;
}

int pbs_compare(const void* t1, const void* t2) {
    const task_t* task1 = t1;
    const task_t* task2 = t2;
    if (task1->tprio == task2->tprio) {
        return compare_by_current_time(t1,t2);
    }
    if (task1->tprio < task2->tprio) {
        return -1;
    }
    return 1;
}


int pbs_should_yield(task_t* task) {
    if (list_empty(&ready)) return 0;
    task_t* front = ready.head_guard.next->data;
    return pbs_compare(front, task) < 0;
}





void srtf_tick() {
    // nothing to do
}

int srtf_compare(const void* t1, const void* t2) {
    const task_t* task1 = t1;
    const task_t* task2 = t2;
    if (task1->remain == task2->remain) {
        return compare_by_current_time(t1,t2);
    }
    if (task1->remain < task2->remain) {
        return -1;
    }
    return 1;
}


int srtf_should_yield(task_t* task) {
    if (list_empty(&ready)) return 0;
    task_t* front = ready.head_guard.next->data;
    return srtf_compare(front, task) < 0;
}




void mlfq_tick() {
    // nothing todo
}

void mlfq_add_ready(task_t* task) {
    if (task->level < 0 || task->bursted >= (task->level + 1) * 5) {
        task->bursted = 0;
        task->level++;
    }
    if (task->level > 4) {
        task->level = 4;
    }
    //printf("add task %d to level %d\n", task->tid, task->level);
    list_append(&mlfq_ready[task->level], task);
}

task_t* mlfq_next_task() {
    for (int i = 0; i < 4; i++) {
        if (!list_empty(&mlfq_ready[i])) {
            return list_popfront(&mlfq_ready[i]);
        }
    }
    return list_popfront(&mlfq_ready[4]);
}

int mlfq_should_yield(task_t* task) {
    for (int i = 0; i < task->level; i++) {
        if (!list_empty(&mlfq_ready[i])) {
            return 1;
        }
    }
    //printf("task %d: bursted %d level %d\n", task->tid, task->bursted, task->level);
    return task->bursted >= (task->level + 1) * 5;
}

int mlfq_compare(const void* t1, const void* t2) {
    // not used
    (void)t1;
    (void)t2;
    return 0;
}



void init_scheduler( int sched_type ) {
    // initialize globals
    running = NULL;
    all_tasks = NULL;
    cur_tick = 0;
    for (int i = 0; i < SEM_ID_MAX; i++) {
        list_init(&waiting[i]);
        sem_values[i] = 0;
    }
    list_init(&ready);
    if (sched_type == MLFQ) {
        for (int i = 0; i < 5; i++) {
            list_init(&mlfq_ready[i]);
        }
    }
    waken_up = NULL;
    pthread_mutex_init(&lock_scheduler, NULL);
    pthread_cond_init(&cond_waken_up, NULL);

    // set callbacks
    switch (sched_type) {
        case FCFS:
            scheduler.on_tick = fcfs_tick;
            scheduler.should_yield = fcfs_should_yield;
            scheduler.compare_task = NULL; // fcfs just append
            scheduler.task_ready = task_ready_common;
            scheduler.next_task = next_task_common;
            break;
        case SRTF:
            scheduler.on_tick = srtf_tick;
            scheduler.should_yield = srtf_should_yield;
            scheduler.compare_task = srtf_compare;
            scheduler.task_ready = task_ready_common;
            scheduler.next_task = next_task_common;
            break;
        case PBS:
            scheduler.on_tick = pbs_tick;
            scheduler.should_yield = pbs_should_yield;
            scheduler.compare_task = pbs_compare;
            scheduler.task_ready = task_ready_common;
            scheduler.next_task = next_task_common;
            break;
        case MLFQ:
            scheduler.on_tick = mlfq_tick;
            scheduler.should_yield = mlfq_should_yield;
            scheduler.compare_task = mlfq_compare;
            scheduler.task_ready = mlfq_add_ready;
            scheduler.next_task = mlfq_next_task;
            break;

    }

}

int schedule_me(float currentTime, int tid, int remainingTime, int tprio) {
    //printf("scheduling at %f\n", currentTime);
    // first to lock the mutex
    pthread_mutex_lock(&lock_scheduler);

    // check if task in system
    task_t* task = task_find(all_tasks, tid);
    // add task to system if not in
    if (!task) {
        //printf("task %d is not in system, add it to system\n", tid);
        // task is new arrival
        task = task_add(&all_tasks, tid, tprio);
        task->arrival = currentTime;
        task->remain = remainingTime;
        // new task added to ready
        scheduler.task_ready(task);
    } else if (waken_up == task) {
        //printf("task %d is just waken up, add to ready queue\n", tid);
        scheduler.task_ready(task);
        waken_up = NULL;
    }
    // record its last sched time
    task->cur_tick = cur_tick;
    task->remain = remainingTime;

    // do scheduler job, for others
    if (!running || scheduler.should_yield(running)) {
        // need to preemption
        if (running) {
            scheduler.task_ready(running);
        }
        running = scheduler.next_task();
        if (!running) {
            // no task in ready queue
            // me is only task that is not blocking,
            // set as running
            running = task;
        } else if (running->tid != tid) {
            // running is not me
            running->bursted = 0;
            // compensate due to running's currentTime
            // is fallen back
            if (running->cur_tick < cur_tick) {
                running->cur_tick = cur_tick + 1;
            }
            pthread_cond_signal(&running->cond);
        }
    }

    while (!running || running->tid != tid) {
        //printf("task %d waiting for its turn\n", tid);
        pthread_cond_wait(&task->cond, &lock_scheduler);
    }
    // check if running's clock
    // if it is advanced, then need update cur_tick
    if (running->cur_tick * 1.0 <= currentTime) {
        // printf("my time is advanced, %f\n", currentTime);
        align_tick(currentTime);
    } else {
        // otherwise, need to update cur tick
        // printf("my time is fall back, %f\n", currentTime);
        cur_tick = running->cur_tick;
    }

    // no time remaining, remove from system
    if (!remainingTime) {
        running = scheduler.next_task();
        if (running) {
            if (running->cur_tick < cur_tick) {
                running->cur_tick = cur_tick;
            }
            pthread_cond_signal(&running->cond);
        }
        task_remove(&all_tasks, tid);
    } else {
        // add bursted by 1
        running->bursted++;
    }


    pthread_mutex_unlock(&lock_scheduler);
    return cur_tick;

}

int P( float currentTime, int tid, int sem_id) { // returns current global time
    task_t* task = task_find(all_tasks, tid);
    pthread_mutex_lock(&lock_scheduler);
    // only running task could P
    while (running->tid != tid) {
        pthread_cond_wait(&task->cond, &lock_scheduler);
    }
    align_tick(currentTime);
    //printf("task %d is blocking on %d\n", tid, sem_id);
    // deduct sem value by 1
    sem_values[sem_id]--;

    // check if need to join queue
    if (sem_values[sem_id] < 0) {
        task->arrival = currentTime; // arrival time here is the time it join the queue
        list_insert(&waiting[sem_id], task, compare_task_by_arrival);
        // waiting for wake up
        running = scheduler.next_task();
        if (running && running->tid != tid) {
            pthread_cond_signal(&running->cond);
        }
        pthread_cond_wait(&task->cond, &lock_scheduler);
        waken_up = task;
    }
    pthread_mutex_unlock(&lock_scheduler);
    return cur_tick;
}

int V( float currentTime, int tid, int sem_id){ // returns current global time
    (void)currentTime;
    task_t* task = task_find(all_tasks, tid);
    pthread_mutex_lock(&lock_scheduler);
    while (running->tid != tid) {
        pthread_cond_wait(&task->cond, &lock_scheduler);
    }
    // add sem value by 1
    sem_values[sem_id]++;
    // check if need to wake up task
    if (list_empty(&waiting[sem_id]) <= 0) {
        task_t* wake_up = list_popfront(&waiting[sem_id]);
        pthread_cond_signal(&wake_up->cond);
    }
    pthread_mutex_unlock(&lock_scheduler);
    return cur_tick;
}
