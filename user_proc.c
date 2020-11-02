#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

// for simulated clock
typedef struct {
    unsigned int simu_seconds;
    unsigned int simu_nanosecs;
} simu_time;

// PCB Table
typedef struct {
    unsigned int pid;  // max is 18
    int priority;
    int onDeck;
    simu_time arrivalTime;
    simu_time cpuTime;
    simu_time sysTime; //Time in the system
    simu_time burstTime;  //Time used in the last burst
    simu_time waitTime;  //Total sleep time. time waiting for an event
} process_table;

//msg struct for msgqueue
typedef struct {
    long mess_ID;
    int mess_quant;
} message;

FILE* logFile;//log file
const key_t PCB_TABLE_KEY = 110594;//key for shared PCB Table
const key_t CLOCK_KEY = 110197;//key for shared simulated clock 
const key_t MSG_KEY = 052455;//key for message queue
int pcbTableId;//shmid for PCB Table
int clockId;//shmid for simulated clock
int msqid;//id for message queue



void increment_sim_time(simu_time* simTime, int increment);
simu_time subtract_sim_times(simu_time a, simu_time b);
simu_time add_sim_times(simu_time a, simu_time b)
simu_time divide_sim_time(simu_time simTime, int divisor);
process_table create_pcb(int priority, int pid, simu_time currentTime);



process_table* attach_pcb_table();
simu_time* attach_sim_clock();
void get_clock_and_table(int n);
int get_outcome();

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: ./user pid msqid quantum\n");
        exit(EXIT_SUCCESS);
    }
    int pid;
    message msg;
    int quantum;
    int outcome = 0;
    pid = atoi(argv[1]);
    msqid = atoi(argv[2]);
    quantum = atoi(argv[3]);
    srand(time(0) + (pid + 1));  // seeding rand. seeding w/ time(0) caused
                                 // processes spawned too close to have same seed
    process_table* table;
    simu_time* simClock;
    simu_time timeBlocked;//holds the time that the process was blocked at
    simu_time event;  // time of the event that will unblock the process
    int burst;//burst to calculate unblock time
    get_clock_and_table(pid);
    table = attach_pcb_table();
    simClock = attach_sim_clock();

    // while loop to wait for messages from oss until we terminate.
    while (outcome != 1) {  // outcome == 1 means terminate
        if ((msgrcv(msqid, &msg, sizeof(msg.mvalue), (pid + 1), 0)) == -1) {
            perror("./user: Error: msgrcv ");
            exit(EXIT_FAILURE);
        }
        /* message response based on if terminating, blocked or neither
         * oss will know process is blocked if mvalue < 0
         *                       is terminating if 0 <= mvalue < 100
         *                       is using full quantum if mvalue == 100
         * */
        outcome = get_outcome();
        switch (outcome) {
        case 0:  // full
            msg.mvalue = 100;
            break;
        case 1:  // term
            msg.mvalue = (rand() % 99) + 1;
            break;
        case 2:  // block
            msg.mvalue = ((rand() % 99) + 1) * -1;
            timeBlocked.simu_seconds= simClock->simu_seconds;
            timeBlocked.simu_nanosecs = simClock->ns;
            burst = msg.mvalue * (quantum / 100) * pow(2.0, (double)table[pid].priority);
            event.simu_seconds= (rand() % 4) + 1;//generating r [0,5]
            event.simu_nanosecs = (rand() % 1000) * 1000000; //generating s [0, 999]. * 1000000 to convert to ns
            // add to wait time total
            table[pid].waitTime = add_sim_times(table[pid].waitTime, event);
            event = add_sim_times(event, timeBlocked);//event time = current time + r.s
            increment_sim_time(&event, (burst * -1));
            // set status to blocked before telling oss to avoid race
            // condition. OSS is waiting for a message response so it
            // cant possibly check our isReady variable yet
            table[pid].isReady = 0;
            break;
        default:
            break;
        }                       // end switch
        msg.mtype = pid + 100;  // oss is waiting for a msg w/ type pid+100
        //printf("USER: sending type: %ld from pid: %d", msg.mtype, pid);
        if (msgsnd(msqid, &msg, sizeof(msg.mvalue), 0) == -1) {
            perror("./user: Error: msgsnd ");
            exit(EXIT_FAILURE);
        }
        //printf(", sent type: %ld from pid: %d\n", msg.mtype, pid);
        // BLocked Outcome
        // already sent message to oss that we are blocked
        // set to
        if (outcome == 2) {
            // while loop to wait for event time to pass
            while (table[pid].isReady == 0) {
                //printf("waiting %ds%9dns\n", event.s, event.ns);
                if (event.simu_seconds > simClock->simu_seconds) {
                    table[pid].isReady = 1;
                }
                else if (event.simu_nanosecs >= simClock->ns && event.simu_seconds >= simClock->simu_seconds) {
                    table[pid].isReady = 1;
                }
            }
        }
    }  // end while. no longer sending or recieving messages
    // printf("%d term\n", pid);
    return 0;
}

process_table* attach_pcb_table() {
    process_table* pcbTable;
    pcbTable = shmat(pcbTableId, NULL, 0);
    if (pcbTableId < 0) {  // error
        perror("./user: Error: shmat ");
        exit(EXIT_FAILURE);
    }
    return pcbTable;
}

simu_time* attach_sim_clock() {
    simu_time* simClock;
    simClock = shmat(clockId, NULL, 0);
    if (clockId < 0) {  // error
        perror("./user: Error: shmat ");
        exit(EXIT_FAILURE);
    }
    return simClock;
}

void get_clock_and_table(int n) {
    // Getting shared memory for the simulated clock
    clockId = shmget(CLOCK_KEY, sizeof(simu_time), IPC_CREAT | 0777);
    if (clockId < 0) {  // error
        perror("./user: Error: shmget ");
        exit(EXIT_FAILURE);
    }
    // Getting shared memory for the pcb table
    pcbTableId = shmget(PCB_TABLE_KEY, sizeof(process_table) * (n + 1), IPC_CREAT | 0777);
    if (pcbTableId < 0) {
        perror("./user: Error: shmget ");
        exit(EXIT_FAILURE);
    }
    return;
}

// 0: not terminating or blocked, 1: terminating, 2:not terminating but blocked
int get_outcome() {
    int tPercent = 5;   //% chance of terminating
    int bPercent = 5;  //% chance of getting blocked
    int terminating = ((rand() % 100) + 1) <= tPercent ? 1 : 0;
    int blocked = ((rand() % 100) + 1) <= bPercent ? 1 : 0;
    if (terminating)
        return 1;
    if (blocked)
        return 2;
    // not blocked or terminating
    return 0;
}

//increment given simulated time by given increment
void increment_sim_time(simu_time* simTime, int increment) {
    simTime->ns += increment;
    if (simTime->ns >= 1000000000) {
        simTime->ns -= 1000000000;
        simTime->s += 1;
    }
}
// returns a - b
simu_time subtract_sim_times(simu_time a, simu_time b) {
    simu_time diff = { .simu_seconds= a.simu_seconds- b.simu_seconds,
                      .simu_nanosecs = a.simu_nanosecs - b.simu_nanosecs };
    if (diff.simu_nanosecs < 0) {
        diff.simu_nanosecs += 1000000000;
        diff.simu_seconds-= 1;
    }
    return diff;
}
//returns a + b
simu_time add_sim_times(simu_time a, simu_time b) {
    simu_time sum = { .simu_seconds= a.simu_seconds + b.simu_seconds,
                      .simu_nanosecs = a.simu_nanosecs + b.simu_nanosecs };
    if (sum.simu_nanosecs >= 1000000000) {
        sum.simu_nanosecs -= 1000000000;
        sum.simu_seconds+= 1;
    }
    return sum;
}

//returns simtime / divisor
simu_time divide_sim_time(simu_time simTime, int divisor) {
    simu_time quotient = { .simu_seconds= simTime.simu_seconds / divisor, .simu_nanosecs = simTime.simu_nanosecs / divisor };
    return quotient;
}

process_table create_pcb(int priority, int pid, simu_time currentTime) {
    process_table pcb = { .pid = pid,
                  .priority = priority,
                  .isReady = 1,
                  .arrivalTime = {.simu_seconds = currentTime.s, .simu_nanosecs = currentTime.ns},
                  .cpuTime = {.simu_seconds = 0, .simu_nanosecs = 0},
                  .sysTime = {.simu_seconds= 0, .simu_nanosecs = 0},
                  .burstTime = {.simu_seconds= 0, .simu_nanosecs = 0},
                  .waitTime = {.simu_seconds= 0, .simu_nanosecs = 0} };
    return pcb;
}