/*
 * Class: CSC389 - Introduction to Operating Systems
 * Assignment: Project 2
 * Author: Frank L. Morales II (fmora3@uis.edu)
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>
// #include <windows.h> /* Uncomment for Windows */

/*
 * Some helpful values used throughout 
 */
enum { WOMAN = 0, MAN, NONE };

typedef struct _person_t {
    int             id;
    int             gender;
    int             bathroom_time;	/* Used in a sleep call to
					 * 'simulate' using the bathroom,
					 * 1-3 seconds */
    struct _person_t *next;
} person;

/*
 * Simple Queue/FIFO structure to manage access to our bathroom line. 
 */
typedef struct _queue_t {
    person         *front;	/* Front of our queue */
    person         *back;	/* Always point to the last element of our 
				 * queue */
    int             num_women;
    int             num_men;
} queue;

/*
 * Basic queue functions & helpers 
 */
void            push(queue * q, person * p);
person         *pop(queue * q);
person         *peek(queue * q);
void            printQueue(queue * q);

/*
 * Our main bathroom structure 
 */
struct _bathroom_t {
    /*
     * This mutex/conditional variable protect our bathroom 
     */
    pthread_mutex_t bathroom_mtx;
    pthread_cond_t  bathroom_cond;
    int             bathroom_occupant_type;	/* WOMAN or MAN */
    bool            bathroom_occupied;	/* YES or NO */
    bool            end_of_students;	/* true or false, default false */
    int             num_using;
    int             cycle;

    queue           bathroom_line;	/* Line of people waiting to use
					 * the bathroom */
};

/*
 * Assigned functions 
 */
void            woman_wants_to_enter(person * p);
void            man_wants_to_enter(person * p);
void            woman_leaves(person * p);
void            man_leaves(person * p);

/*
 * These process the cycle of each individual from 'entering the
 * queue/using the bathroom to done' 
 */
void           *create_student(void *thread_id);
void           *bathroom_attendant(void *thread_id);
void           *use_bathroom(void *person);

/*
 * Main student bathroom all of our "students" will use 
 */
struct _bathroom_t bathroom;

/*
 * Generic function that prints output along the way per the requirements 
 */
void            showProgress();

int
main(int argc, char *argv[])
{
    /*
     * Our program has 2 threads that create/move students through the
     * bathroom 
     */
    pthread_t       create_thread,
                    attendant_thread;
    int             thread_ids[2] = { 1, 2 };

    /*
     * Seed our random number generator for use throughout our program 
     */
    srand(time(NULL));

    /*
     * Initialize our bathroom 
     */
    bathroom.end_of_students = false;
    bathroom.bathroom_occupant_type = NONE;
    bathroom.bathroom_occupied = false;
    bathroom.cycle = 0;
    bathroom.num_using = 0;
    bathroom.bathroom_line.front = NULL;
    bathroom.bathroom_line.back = NULL;
    bathroom.bathroom_line.num_men = 0;
    bathroom.bathroom_line.num_women = 0;

    /*
     * Initialize bathroom mutex/conditionals here 
     */
    pthread_mutex_init(&bathroom.bathroom_mtx, NULL);
    pthread_cond_init(&bathroom.bathroom_cond, NULL);

    /*
     * Create threads here 
     */
    pthread_create(&create_thread, NULL, create_student, &thread_ids[0]);
    pthread_create(&attendant_thread, NULL, bathroom_attendant,
		   &thread_ids[1]);

    /*
     * Join/wait for our threads exit value(s) 
     */
    pthread_join(create_thread, NULL);
    pthread_join(attendant_thread, NULL);

    puts("\nAll students have finished using the bathroom, exiting.");

    /*
     * Cleanup our mutex/conditional variables 
     */
    pthread_mutex_destroy(&bathroom.bathroom_mtx);
    pthread_cond_destroy(&bathroom.bathroom_cond);

    /*
     * Exit our main program, we're done 
     */
    return 0;
}

/*
 * This is our "producer" thread which creates each student, then places
 * them in the queue 
 */
void           *
create_student(void *thread_id)
{
    int             num_students_created = 0;
    int             rndSleep = 0;
    person         *new_student;

    /*
     * By default we simulate 20 students and exit 
     */
    do {
    	/*
    	 * Allocate & initialize a new student 
    	 */
    	new_student = malloc(sizeof *new_student);
    	new_student->bathroom_time = (rand() % 3) + 1;
    	new_student->gender = ((rand() % 2) + 1) == 1 ? WOMAN : MAN;
    	new_student->id = num_students_created;
    	new_student->next = NULL;

    	pthread_mutex_lock(&bathroom.bathroom_mtx);
    	if (new_student->gender == WOMAN)
    	    woman_wants_to_enter(new_student);
    	else
    	    man_wants_to_enter(new_student);
    	pthread_cond_signal(&bathroom.bathroom_cond);
    	pthread_mutex_unlock(&bathroom.bathroom_mtx);

    	num_students_created++;
    	rndSleep = (rand() % 3) + 1;
    	sleep(rndSleep);	/* Simulate time between students arriving */
    	// Sleep(rndSleep * 1000); /* Uncomment for Windows */
    } while (num_students_created < 10);

    /*
     * We're done generating students, mark our bathroom accordingly and
     * exit 
     */
    pthread_mutex_lock(&bathroom.bathroom_mtx);
    bathroom.end_of_students = true;
    pthread_cond_signal(&bathroom.bathroom_cond);
    pthread_mutex_unlock(&bathroom.bathroom_mtx);
    pthread_exit(NULL);
}

/*
 * Pull students out of queue and launch them in their own thread to "run" 
 */
void           *
bathroom_attendant(void *thread_id)
{
    person         *student;
    pthread_t       student_thread;
    bool            done_processing = false;
    queue           local_queue;	/* Used as a temporary 'storage'
					 * place */

    local_queue.front = NULL;
    local_queue.back = NULL;
    /*
     * These are initialized, but we do not use them in here 
     */
    local_queue.num_men = 0;
    local_queue.num_women = 0;

    /*
     * We continue indefinitely, only exiting when our queue is empty, and 
     * end_of_students is TRUE 
     */
    while (1) {
    	pthread_mutex_lock(&bathroom.bathroom_mtx);	/* BLOCK waiting */

    	if (bathroom.bathroom_line.num_men == 0
    	    && bathroom.bathroom_line.num_women == 0) {
    	    if (bathroom.end_of_students) {
        		pthread_mutex_unlock(&bathroom.bathroom_mtx);
        		break;		/* We're all done processing, break our
    				         * while and exit */
    	    } else {
    		/*
    		 * There's a chance 'end_of_students' was set in between
    		 * conditional waiting, if so do NOT wait again as there
    		 * will never be another student created thus
    		 * num_men/num_women will NEVER be non-zero. 
    		 */
    		while (bathroom.bathroom_line.num_men == 0 &&
    		       bathroom.bathroom_line.num_women == 0 &&
    		       !bathroom.end_of_students)
    		    pthread_cond_wait(&bathroom.bathroom_cond,
    				      &bathroom.bathroom_mtx);
    	    }
    	}

    	/*
    	 * There's a chance that num_men/num_women are zero, but
    	 * 'create_student' hasn't had a chance to set 'end_of_students'
    	 * to 'true' yet in our above conditional wait. So once we receive 
    	 * the conditional wake up, double check if that indeed happened, 
    	 * if so unlock and break. 
    	 */
    	if (bathroom.end_of_students) {
    	    pthread_mutex_unlock(&bathroom.bathroom_mtx);
    	    break;
    	}

    	/*
    	 * Even if 'end_of_students' is now TRUE there SHOULD be at least
    	 * one more student in the queue We should NEVER encounter a case 
    	 * where we get here and there's no student in the queue 
    	 */

    	/*
    	 * Down here this first peek should NEVER be NULL 
    	 */
    	student = peek(&bathroom.bathroom_line);

    	if (bathroom.bathroom_occupied) {
    	    if (student->gender != bathroom.bathroom_occupant_type) {
    		/*
    		 * First person in queue does NOT match who's in the
    		 * bathroom; must wait until the bathroom is empty 
    		 */
    		while (bathroom.bathroom_occupied)
    		    pthread_cond_wait(&bathroom.bathroom_cond,
    				      &bathroom.bathroom_mtx);

    		/*
    		 * Once it's empty, initialize the bathroom for our peek'd 
    		 * student 
    		 */
    		bathroom.bathroom_occupant_type = student->gender;
    		bathroom.bathroom_occupied = true;
    	    }

    	} else {
    	    /*
    	     * Bathroom is empty, initialize the bathroom for our peek'd
    	     * student 
    	     */
    	    bathroom.bathroom_occupant_type = student->gender;
    	    bathroom.bathroom_occupied = true;
    	}

    	/*
    	 * At least the one person at the front of the queue is of the
    	 * same gender and can come over 
    	 */
    	do {
    	    student = pop(&bathroom.bathroom_line);

    	    printf
    		("-- A %s whose ID is %d, is entering the bathroom.\n",
    		 student->gender == WOMAN ? "woman" : "man", student->id);

    	    bathroom.cycle++;
    	    bathroom.num_using++;

    	    if (student->gender == WOMAN)
    		bathroom.bathroom_line.num_women--;
    	    else if (student->gender == MAN)
    		bathroom.bathroom_line.num_men--;

    	    /*
    	     * No need to track local queue counts, it's only temporary 
    	     */
    	    push(&local_queue, student);

    	    showProgress();

    	    student = peek(&bathroom.bathroom_line);
    	} while (student
    		 && student->gender == bathroom.bathroom_occupant_type);

    	/*
    	 * Release our lock, we are starting our threads we no longer need 
    	 * to hold it 
    	 */
    	pthread_mutex_unlock(&bathroom.bathroom_mtx);

    	/*
    	 * We now have all the students that are able to use the bathroom
    	 * right now, spawn each in their own thread 
    	 */
    	student = pop(&local_queue);
    	while (student) {
    	    /*
    	     * Now we create a new thread passing the student as the
    	     * argument 
    	     */
    	    pthread_create(&student_thread, NULL, use_bathroom,
    			   (void *) student);

    	    /*
    	     * Immediately detach it 
    	     */
    	    pthread_detach(student_thread);

    	    student = pop(&local_queue);
    	}
    }

    /*
     * Wait until ALL of our students are done using the bathroom 
     */
    pthread_mutex_lock(&bathroom.bathroom_mtx);
    while (bathroom.num_using > 0)
	   pthread_cond_wait(&bathroom.bathroom_cond, &bathroom.bathroom_mtx);

    /*
     * All students are done, unlock and return 
     */
    pthread_mutex_unlock(&bathroom.bathroom_mtx);
    pthread_exit(NULL);
}

/*
 * A thread function that each person who's currently using the bathroom
 * will use 
 */
void           *
use_bathroom(void *pVoid)
{
    person         *p = (person *) pVoid;

    /*
     * Simulate this person using the bathroom, sleep on their
     * bathroom_time 
     */
    sleep(p->bathroom_time);
    // Sleep(p->bathroom_time * 1000); /* Uncomment for Windows */

    pthread_mutex_lock(&bathroom.bathroom_mtx);	/* BLOCK waiting */

    /*
     * Process the man/woman leaving the bathroom, then exit 
     */
    if (p->gender == WOMAN)
	   woman_leaves(p);
    else if (p->gender == MAN)
	   man_leaves(p);

    pthread_cond_signal(&bathroom.bathroom_cond);
    pthread_mutex_unlock(&bathroom.bathroom_mtx);

    /*
     * This person is done using the bathroom, stop this thread 
     */
    pthread_exit(NULL);
}

/*
 * Called when a new female student wants to enter, she starts in the
 * queue first. NOT THREAD SAFE, HOLD LOCK 
 */
void
woman_wants_to_enter(person * p)
{
    /*
     * Add our new woman to the queue 
     */
    push(&bathroom.bathroom_line, p);
    bathroom.bathroom_line.num_women++;
    bathroom.cycle++;

    /*
     * Update our progress 
     */
    showProgress();
}

/*
 * Called when a new male student wants to enter, he starts in the queue
 * first. NOT THREAD SAFE, HOLD LOCK 
 */
void
man_wants_to_enter(person * p)
{
    /*
     * Add our new man to the queue 
     */
    push(&bathroom.bathroom_line, p);
    bathroom.bathroom_line.num_men++;
    bathroom.cycle++;

    /*
     * Update our progress 
     */
    showProgress();
}

/*
 * Only called when a woman is leaving the bathroom. NOT THREAD SAFE, HOLD 
 * LOCK 
 */
void
woman_leaves(person * p)
{
    bathroom.num_using--;

    /*
     * We must be the last thread using the bathroom, reset accordingly 
     */
    if (bathroom.num_using == 0) {
    	bathroom.bathroom_occupied = false;
    	bathroom.bathroom_occupant_type = NONE;
    	bathroom.cycle++;
    }

    printf
	("-- A woman, ID: %d, is leaving the bathroom, she used it for %d seconds.\n",
	 p->id, p->bathroom_time);

    /*
     * Update our progress 
     */
    showProgress();

    /*
     * Free our person's resources 
     */
    free(p);
}

/*
 * Only called when a man is leaving the bathroom. NOT THREAD SAFE, HOLD
 * LOCK 
 */
void
man_leaves(person * p)
{
    bathroom.num_using--;

    /*
     * We must be the last thread using the bathroom, reset accordingly 
     */
    if (bathroom.num_using == 0) {
    	bathroom.bathroom_occupied = false;
    	bathroom.bathroom_occupant_type = NONE;
    	bathroom.cycle++;
    }

    printf
	("-- A man, ID %d, is leaving the bathroom, he used it for %d seconds.\n",
	 p->id, p->bathroom_time);

    /*
     * Update our progress 
     */
    showProgress();

    /*
     * Free our person's resources 
     */
    free(p);
}

/*
 * Push person 'p' onto queue 'q' in standard FIFO fashion. NOT THREAD
 * SAFE, HOLD LOCK 
 */
void
push(queue * q, person * p)
{
    /*
     * Add our new person to our queue 
     */
    if (q->front == NULL) {
    	/*
    	 * Queue is empty, insert this person at the front/back 
    	 */
    	q->front = p;
    	q->back = p;
    } else {
    	/*
    	 * The queue is NOT empty, point the last person and our back
    	 * pointer to our new person 
    	 */
    	q->back->next = p;
    	q->back = p;
    }
}

/*
 * Remove a person properly, if any, from the front of our queue. NOT
 * THREAD SAFE, HOLD LOCK 
 */
person         *
pop(queue * q)
{
    person         *p = q->front;

    /*
     * If p is valid we must update our queue totals 
     */
    if (p != NULL) {
    	/*
    	 * If this is NOT the last person in our queue, set front to point 
    	 * to who he points to 
    	 */
    	if (p->next != NULL)
    	    q->front = p->next;
    	/*
    	 * Otherwise set our front and back pointer to NULL 
    	 */
    	else
    	    q->front = q->back = NULL;
    }

    /*
     * Just return p, the caller should determine if it's NULL or not 
     */
    return p;
}

/*
 * Simply returns the front node, if any, in our queue. NOT THREAD SAFE,
 * HOLD LOCK 
 */
person         *
peek(queue * q)
{
    return q->front;
}

/*
 * Simple function to traverse/print each person in our queue. NOT THREAD
 * SAFE, HOLD LOCK 
 */
void
printQueue(queue * q)
{
    person         *p = q->front;
    int             queue_position = 0;

    puts("Queue:");
    /*
     * While we have a valid next person in line, print out their info 
     */
    while (p) {
    	printf("     Gender: %s, Queue position: %d\n",
    	       p->gender == WOMAN ? "Woman" : "Man", queue_position + 1);
    	queue_position++;
    	p = p->next;
    }
}

/*
 * Print a message at various places, per the output requirements. NOT
 * THREAD SAFE, HOLD LOCK 
 */
void
showProgress()
{
    int             num_in_line =
	bathroom.bathroom_line.num_men + bathroom.bathroom_line.num_women;

    printf("\nCurrent cycle count: %d\n"
	   "Bathroom occupied by %d %s\nNumber of people in line: %d\n",
	   bathroom.cycle, bathroom.num_using,
	   bathroom.bathroom_occupant_type == WOMAN ? "Women" :
	   bathroom.bathroom_occupant_type == MAN ? "Men" : "Empty",
	   num_in_line);

    /*
     * If anyone is in our queue, print their order + gender 
     */
    if (num_in_line > 0)
	   printQueue(&bathroom.bathroom_line);

    puts("");			/* Just helpful for formatting */
}
