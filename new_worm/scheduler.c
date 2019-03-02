#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED

#include "scheduler.h"

#include <assert.h>
#include <curses.h>
#include <ucontext.h>

#include "util.h"

// Status
#define is_running 1
#define blocked_on_input 2
#define blocked_on_sleep 3
#define blocked_on_join 4
#define exited 5
#define ready 6

// This is an upper limit on the number of tasks we can create.
#define MAX_TASKS 128

// This is the size of each task's stack memory
#define STACK_SIZE 65536

// This struct will hold the all the necessary information for each task
typedef struct task_info {
  // This field stores all the state required to switch back to this task
  ucontext_t context;

  // This field stores another context. This one is only used when the task
  // is exiting.
  ucontext_t exit_context;

  int status;
  size_t wake_time;
  task_t join_thread;
  int input;
  // TODO: Add fields here so you can:
  //   a. Keep track of this task's state.
  //   b. If the task is sleeping, when should it wake up?
  //   c. If the task is waiting for another task, which task is it waiting for?
  //   d. Was the task blocked waiting for user input? Once you successfully
  //      read input, you will need to save it here so it can be returned.
} task_info_t;

int current_task = 0;          //< The handle of the currently-executing task
int num_tasks = 1;             //< The number of tasks created so far
task_info_t tasks[MAX_TASKS];  //< Information for every task

void schedule(task_t t) {
  task_info_t current_task_info = tasks[t];
  task_t index = (t + 1) % num_tasks;
  while (true) {
    if (tasks[index].status == ready) {
      tasks[index].status = is_running;
      current_task = index;
      break;
    } else if (tasks[index].status == blocked_on_sleep) {
      if (time_ms() >= tasks[index].wake_time) {
        tasks[index].status = is_running;
        current_task = index;
        break;
      }
    } else if (tasks[index].status == blocked_on_join) {
      if (t == tasks[index].join_thread) {
        tasks[index].status = is_running;
        current_task = index;
        break;
      }
    } else if (tasks[index].status == blocked_on_input) {
      int res = getch();
      if (res != ERR) {
        tasks[index].status = is_running;
        current_task = index;
        tasks[index].input = res;
        break;
      }
    }

    index = (index + 1) % num_tasks;
    sleep_ms(1);
  }

  if (index == current_task) {
    return;
  } else {
    swapcontext(&(current_task_info.context), &(tasks[index].context));
  }
}
/**
 * Initialize the scheduler. Programs should call this before calling any other
 * functiosn in this file.
 */

void scheduler_init() {
  // TODO: Initialize the state of the scheduler
  tasks[0].status = is_running;
}

/**
 * This function will execute when a task's function returns. This allows you
 * to update scheduler states and start another task. This function is run
 * because of how the contexts are set up in the task_create function.
 */
void task_exit() {
  // TODO: Handle the end of a task's execution here
  tasks[current_task].status = exited;
  schedule(current_task);
}

/**
 * Create a new task and add it to the scheduler.
 *
 * \param handle  The handle for this task will be written to this location.
 * \param fn      The new task will run this function.
 */
void task_create(task_t* handle, task_fn_t fn) {
  // Claim an index for the new task
  int index = num_tasks;
  num_tasks++;

  // Set the task handle to this index, since task_t is just an int
  *handle = index;

  // We're going to make two contexts: one to run the task, and one that runs at
  // the end of the task so we can clean up. Start with the second

  // First, duplicate the current context as a starting point
  getcontext(&tasks[index].exit_context);

  // Set up a stack for the exit context
  tasks[index].exit_context.uc_stack.ss_sp = malloc(STACK_SIZE);
  tasks[index].exit_context.uc_stack.ss_size = STACK_SIZE;

  // Set up a context to run when the task function returns. This should call
  // task_exit.
  makecontext(&tasks[index].exit_context, task_exit, 0);

  // Now we start with the task's actual running context
  getcontext(&tasks[index].context);

  // Allocate a stack for the new task and add it to the context
  tasks[index].context.uc_stack.ss_sp = malloc(STACK_SIZE);
  tasks[index].context.uc_stack.ss_size = STACK_SIZE;

  // Now set the uc_link field, which sets things up so our task will go to the
  // exit context when the task function finishes
  tasks[index].context.uc_link = &tasks[index].exit_context;

  // And finally, set up the context to execute the task function
  makecontext(&tasks[index].context, fn, 0);

  tasks[index].status = ready;
}

/**
 * Wait for a task to finish. If the task has not yet finished, the scheduler
 * should
 * suspend this task and wake it up later when the task specified by handle has
 * exited.
 *
 * \param handle  This is the handle produced by task_create
 */
void task_wait(task_t handle) {
  // TODO: Block this task until the specified task has exited.
  tasks[current_task].status = blocked_on_join;
  tasks[current_task].join_thread = handle;
  schedule(current_task);
}

/**
 * The currently-executing task should sleep for a specified time. If that time
 * is larger
 * than zero, the scheduler should suspend this task and run a different task
 * until at least
 * ms milliseconds have elapsed.
 *
 * \param ms  The number of milliseconds the task should sleep.
 */
void task_sleep(size_t ms) {
  // TODO: Block this task until the requested time has elapsed.
  // Hint: Record the time the task should wake up instead of the time left for
  // it to sleep. The bookkeeping is easier this way.
  tasks[current_task].status = blocked_on_sleep;
  tasks[current_task].wake_time = time_ms() + ms;
  schedule(current_task);
}

/**
 * Read a character from user input. If no input is available, the task should
 * block until input becomes available. The scheduler should run a different
 * task while this task is blocked.
 *
 * \returns The read character code
 */
int task_readchar() {
  // TODO: Block this task until there is input available.
  // To check for input, call getch(). If it returns ERR, no input was
  // available.
  // Otherwise, getch() will returns the character code that was read.
  tasks[current_task].status = blocked_on_input;
  schedule(current_task);
  return tasks[current_task].input;
}
