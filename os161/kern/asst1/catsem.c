/*
 * catsem.c
 *
 * 30-1-2003 : GWA : Stub functions created for CS161 Asst1.
 *
 * NB: Please use SEMAPHORES to solve the cat syncronization problem in 
 * this file.
 */


/*
 * 
 * Includes
 *
 */

#include <types.h>
#include <lib.h>
#include <test.h>
#include <thread.h>
#include <synch.h>


/*
 * 
 * Constants
 *
 */

/*
 * Number of food bowls.
 */

#define NFOODBOWLS 2

/*
 * Number of cats.
 */

#define NCATS 6

/*
 * Number of mice.
 */

#define NMICE 2


#define N_ITERATIONS 4

/*
 * 
 * Function Definitions
 * 
 */

struct semaphore* sem;
struct semaphore* mutex;
struct semaphore* thread_lock;
int num_mice_eating = 0;
int num_cat_eating = 0;
int cat_dish_select = 0;
int mice_dish_select = 0;


enum dish_available
{
    dish1_avail,
    dish2_avail,
    not_avail    
};

dish_available[NFOODBOWLS];

/* who should be "cat" or "mouse" */
static void
sem_eat(const char *who, int num, int bowl, int iteration)
{
        kprintf("%s: %d starts eating: bowl %d, iteration %d\n", who, num, 
                bowl, iteration);
        clocksleep(1);
        kprintf("%s: %d ends eating: bowl %d, iteration %d\n", who, num, 
                bowl, iteration);
}

/*
 * catsem()
 *
 * Arguments:
 *      void * unusedpointer: currently unused.
 *      unsigned long catnumber: holds the cat identifier from 0 to NCATS - 1.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      Write and comment this function using semaphores.
 *
 */

static
void
catsem(void * unusedpointer, 
       unsigned long catnumber)
{
    (void)unusedpointer;

    int iteration;
    for(iteration = 0; iteration < N_ITERATIONS; iteration++)
    {   
        // ensure atomic checking and modifying state variables

        if(num_mice_eating <= 0)
            P(thread_lock);
        else{
            iteration--;
            continue;
        }
        /*  the cat is ready to eat. i.e. mice have left . */
        num_cat_eating++;
        V(thread_lock);
        // we allow two threads to enter
        P(sem);
        // lock for globals
        P(mutex);
        // find an available dish
        if(dish_available[0] == dish1_avail)
        {
            cat_dish_select = 1;
            dish_available[0] = not_avail;
            V(mutex);
            sem_eat("cat", catnumber, 1, iteration);
        }
        else if(dish_available[1] == dish2_avail)
        {
            cat_dish_select = 2;
            dish_available[1] = not_avail;
            V(mutex);
            sem_eat("cat", catnumber, 2, iteration);
        }   
        // release lock to let the other thread that has entered the semaphore to proceed
        clocksleep(1);

        /* here we reset state variables and notify other threads that the current thread is done. */
        P(mutex);
        if(cat_dish_select == 1)
            dish_available[0] = dish1_avail;
        else if(cat_dish_select == 2)
            dish_available[1] = dish2_avail;
        cat_dish_select = 0;
        num_cat_eating--;
        V(mutex);
        V(sem);
    }
        
}
        

/*
 * mousesem()
 *
 * Arguments:
 *      void * unusedpointer: currently unused.
 *      unsigned long mousenumber: holds the mouse identifier from 0 to 
 *              NMICE - 1.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      Write and comment this function using semaphores.
 *
 */

static
void
mousesem(void * unusedpointer, 
         unsigned long mousenumber)
{
    (void) unusedpointer;
     int iteration;
    for(iteration = 0; iteration < N_ITERATIONS; iteration++)
    {

       if(num_cat_eating <= 0)
            P(thread_lock);
        else{
            iteration--;
            continue;
        }
        /*  the mouse is ready to eat. i.e. cats have left . */
        num_mice_eating++:
        V(thread_lock);

         P(sem);
        // lock for globals
        P(mutex);

        if(dish_available[0] == dish1_avail)
        {
            cat_dish_select = 1;
            dish_available[0] = not_avail;
            V(mutex);
            sem_eat("cat",catnumber, 1,iteration);
        }
        else if(dish_available[1] == dish2_avail)
        {
            cat_dish_select = 2;
            dish_available[1] = not_avail;
            V(mutex);
            sem_eat("cat",catnumber, 2,iteration);
        }   

        // release lock to let the other thread that has entered the semaphore to proceed
        clocksleep(1);

        /* here we reset state variables and notify other threads that the current thread is done. */
        P(mutex);
        if(cat_dish_select == 1)
            dish_available[0] = dish1_avail;
        else if(cat_dish_select == 2)
            dish_available[1] = dish2_avail;
        cat_dish_select = 0;
        num_mice_eating--;
        V(mutex);
        V(sem);
    }
        
}


/*
 * catmousesem()
 *
 * Arguments:
 *      int nargs: unused.
 *      char ** args: unused.
 *
 * Returns:
 *      0 on success.
 *
 * Notes:
 *      Driver code to start up catsem() and mousesem() threads.  Change this 
 *      code as necessary for your solution.
 */

int
catmousesem(int nargs,
            char ** args)
{
        int index, error;
   
        /*
         * Avoid unused variable warnings.
         */

        (void) nargs;
        (void) args;
   
        sem = sem_create("sem", 2);
        metex = sem_create("lock", 1);
        thread_lock = sem_create("thread_lock", 1);
        dish_available[0] = dish1_avail;
        dish_available[1] = dish2_avail;

        /*
         * Start NCATS catsem() threads.
         */
        for (index = 0; index < NCATS; index++) {
           
                error = thread_fork("catsem Thread", 
                                    NULL, 
                                    index, 
                                    catsem, 
                                    NULL
                                    );
                
                /*
                 * panic() on error.
                 */

                if (error) {
                 
                        panic("catsem: thread_fork failed: %s\n", 
                              strerror(error)
                              );
                }
        }
        
        /*
         * Start NMICE mousesem() threads.
         */

        for (index = 0; index < NMICE; index++) {
   
                error = thread_fork("mousesem Thread", 
                                    NULL, 
                                    index, 
                                    mousesem, 
                                    NULL
                                    );
                
                /*
                 * panic() on error.
                 */

                if (error) {
         
                        panic("mousesem: thread_fork failed: %s\n", 
                              strerror(error)
                              );
                }
        }
        sem_destroy(sem);
        sem_destroy(mutex);
        return 0;
}


/*
 * End of catsem.c
 */
