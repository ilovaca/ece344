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

static struct semaphore* sem;
// static struct semaphore* mouse_sem;
static struct semaphore* thread_lock;
static struct semaphore* join_count;
static int num_mice_eating = 0;
static int num_cat_eating = 0;


typedef enum 
{
    dish1_avail,
    dish2_avail,
    not_avail    
}dish_availability;

static dish_availability dish_available[NFOODBOWLS];

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
   // kprintf("cat %d is coming\n",catnumber);
    (void)unusedpointer;
    int cat_dish_select = 0;
    int iteration;
    for(iteration = 0; iteration < N_ITERATIONS; iteration++)
    {       
        P(sem);
        P(thread_lock); 
        if(num_mice_eating>0){
            V(thread_lock);
            iteration--;
            continue;
        }
        
           
        // From now on, there could be 2 cats. So we need to lock
        // P(thread_lock);
        num_cat_eating++;
        // kprintf("the number of cat eating now is %d\n",num_cat_eating);
        // V(thread_lock);
        // we allow two threads to enter
        // P(sem);
        // kprintf("after require, sem->count = %d, and the cat thread is %d \n", sem->count,catnumber);
        // lock for globals
        // P(mutex);

        // find an available dish
        if(dish_available[0] == dish1_avail)
        {
            cat_dish_select = 1;
            dish_available[0] = not_avail;
            // Done modifying the state, we can let the other cat proceed
            V(thread_lock);
            sem_eat("cat", catnumber, 1, iteration);
        }
        else if(dish_available[1] == dish2_avail)
        {
            cat_dish_select = 2;
            dish_available[1] = not_avail;
            V(thread_lock);
            sem_eat("cat", catnumber, 2, iteration);
        }   

        // done eating, we change state accordingly
        P(thread_lock);
        if(cat_dish_select == 1)
            dish_available[0] = dish1_avail;
        else if(cat_dish_select == 2)
            dish_available[1] = dish2_avail;
        cat_dish_select = 0;
        num_cat_eating--;
        V(thread_lock);
        V(sem);
    }
     V(join_count);
        
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
   //  kprintf("mouse %d is coming\n",mousenumber);
    (void) unusedpointer;
    int iteration;
    int mice_dish_select = 0;
    for(iteration = 0; iteration < N_ITERATIONS; iteration++)
    {

        // if(num_cat_eating <= 0)
        P(sem);
        P(thread_lock);
        if(num_cat_eating>0){
            V(thread_lock);
            iteration--;
            continue;
        }
        
       
        // else{
            // iteration--;
            // continue;
        // }
        /*  the mouse is ready to eat. i.e. cats have left . */
        // V(thread_lock);

        // P(thread_lock);
        num_mice_eating++;

        if(dish_available[0] == dish1_avail)
        {
            mice_dish_select = 1;
            dish_available[0] = not_avail;
            V(thread_lock);
            sem_eat("mouse", mousenumber, 1, iteration);
        }
        else if(dish_available[1] == dish2_avail)
        {
            mice_dish_select = 2;
            dish_available[1] = not_avail;
            V(thread_lock);
            sem_eat("mouse", mousenumber, 2, iteration);
        }   

        P(thread_lock);
        if(mice_dish_select == 1)
            dish_available[0] = dish1_avail;
        else if(mice_dish_select == 2)
            dish_available[1] = dish2_avail;
        mice_dish_select = 0;
        num_mice_eating--;
        V(thread_lock);
        V(sem);
    }
    V(join_count);
        
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
        thread_lock = sem_create("thread_lock", 1);
        join_count = sem_create("join_count",0);
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

        int i;
        for(i=0;i<8;i++) P(join_count);
        sem_destroy(sem);
        sem_destroy(thread_lock);
        sem_destroy(join_count);
        return 0;
}


/*
 * End of catsem.c
 */
