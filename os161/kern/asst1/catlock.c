/*
 * catlock.c
 *
 * 30-1-2003 : GWA : Stub functions created for CS161 Asst1.
 *
 * NB: Please use LOCKS/CV'S to solve the cat syncronization problem in 
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

/* Locks */
struct lock * dish_lock;
/* CVs */
struct cv* dish_cv;

int num_free_dishes = NFOODBOWLS;
// int num_cat_waiting = 0;
int num_mice_eating = 0;
int cat_dish_select = 0;
int mice_dish_select = 0;

enum dish_status
{
    non_eating,
    cat_eating,
    mouse_eating
};

dish_status dish_statuses [NFOODBOWLS];

enum dish_availability
{
    dish1_avail,
    dish2_avail,
    not_avail    
};

dish_availability dish_available[NFOODBOWLS];


/*
 * 
 * Function Definitions
 * 
 */

/* who should be "cat" or "mouse" */
static void
lock_eat(const char *who, int num, int bowl, int iteration)
{
        kprintf("%s: %d starts eating: bowl %d, iteration %d\n", who, num, 
                bowl, iteration);
        clocksleep(1);
        kprintf("%s: %d ends eating: bowl %d, iteration %d\n", who, num, 
                bowl, iteration);
}

/*
 * catlock()
 *
 * Arguments:
 *      void * unusedpointer: currently unused.
 *      unsigned long catnumber: holds the cat identifier from 0 to NCATS -
 *      1.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      Write and comment this function using locks/cv's.
 *
 */

static
void
catlock(void * unusedpointer, 
        unsigned long catnumber)
{
    (void) unusedpointer;
    int iteration;
    for (iteration = 0; iteration < N_ITERATIONS; iteration ++) {
            /* First grab the dish lock */
            lock_acquire(dish_lock);

             
            // wait until the all mice have left the dish and 
            while (dish_statuses[0] == mouse_eating || dish_statuses[1] == mouse_eating || num_free_dishes <= 0 || num_mice_eating > 0) {
                cv_wait(dish_cv, dish_lock);
            }
           
            // find the first available dish
            if (dish_available[0] == dish1_avail) {
                cat_dish_select = 1;
                dish_statuses[0] = cat_eating;
                num_free_dishes--;
                lock_release(dish_lock);
                lock_eat("cat", catnumber, 1, iteration);
                
            } else if (dish_available[1] == dish2_avail) {
                cat_dish_select = 2;
                dish_statuses[1] = cat_eating;
                num_free_dishes--;
                lock_release(dish_lock);
                lock_eat("cat", catnumber, 2, iteration);
            
            } else {
                // none available is an error
                assert(num_free_dishes <= 0);
            }

          //  clocksleep(1); //give up CPU to let other thread process.

            /* Here we rerset state variables and notify other threads that this cat is done. */
            lock_acquire(dish_lock);
            num_free_dishes++;
            if(cat_dish_select == 1)
                dish_statuses[0] = non_eating;
            else if(cat_dish_select == 2)
                dish_statuses[1] = non_eating;
            cat_dish_select = 0;
            cv_broadcast(dish_cv, dish_lock); 
            lock_release(dish_lock);
    }
}
	

/*
 * mouselock()
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
 *      Write and comment this function using locks/cv's.
 *
 */

static
void
mouselock(void * unusedpointer,
          unsigned long mousenumber)
{
    (void) unusedpointer;
    int iteration;
    for (iteration = 0; iteration < N_ITERATIONS; iteration ++) {

        lock_acquire(dish_lock);
        // why do you have to wait until no cats are waiting?
        while (num_free_dishes <= 0 || dish_statuses[0] == cat_eating || dish_statuses[1] == cat_eating){
               cv_wait(dish_cv, dish_lock);
         }
        /* here the mice gets the lock and ready to eat */
         num_mice_eating++;

        if (dish_available[0] == dish1_avail) {
                mice_dish_select = 1;
                dish_statuses[0] = mouse_eating;
                num_free_dishes--;
                lock_release(lock);
                lock_eat("mouse",mousenumber, 1, iteration);
                /* reset num_free_dishes and dish_statuses[0] */    
            
            } else if (dish_available[1] == dish2_avail) {
                mice_dish_select = 2;
                dish_statuses[1] = mouse_eating;
                num_free_dishes--;
                lock_release(lock);
                lock_eat("mouse", mousenumber, 2, iteration);
                 /* reset num_free_dishes and dish_statuses[0] */
                
            } else {
                // none available is an error
                assert(num_free_dishes <= 0);
            }
         //   clocksleep(1);
            /* Here we rerset state variables and notify other threads that the current thread is done. */
            lock_acquire(lock);
            num_free_dishes++;
            num_mice_eating--;
            if(mice_dish_select == 1)
                dish_statuses[0] = non_eating;
             else if(mice_dish_select == 2)
                dish_statuses[1] = non_eating;
            mice_dish_select = 0;
            cv_broadcast(dish_cv, dish_lock); //notify other threads that the current thread is done.
            lock_release(dish_lock);
    }    

}


/*
 * catmouselock()
 *
 * Arguments:
 *      int nargs: unused.
 *      char ** args: unused.
 *
 * Returns:
 *      0 on success.
 *
 * Notes:
 *      Driver code to start up catlock() and mouselock() threads.  Change
 *      this code as necessary for your solution.
 */

int
catmouselock(int nargs,
             char ** args)
{
        int index, error;
   
        /*
         * Avoid unused variable warnings.
         */

        (void) nargs;
        (void) args;
   
        /* intiailization code */
        dish_lock = lock_create("dish_lock");

        dish_cv = cv_create("dish_cv");

        dish_statuses[0] = non_eating;
        dish_statuses[1] = non_eating;
        dish_available[0] = dish1_avail;
        dish_available[1] = dish2_avail;
        /*
         * Start NCATS catlock() threads.
         */

        for (index = 0; index < NCATS; index++) {
           
                error = thread_fork("catlock thread", 
                                    NULL, 
                                    index, 
                                    catlock, 
                                    NULL
                                    );

                /*thread_fork(const char *name, 
        void *data1, unsigned long data2,
        void (*func)(void *, unsigned long),
        struct thread **ret)*/
                
                /*
                 * panic() on error.
                 */

                if (error) {
                 
                        panic("catlock: thread_fork failed: %s\n", 
                              strerror(error)
                              );
                }
        }

        /*
         * Start NMICE mouselock() threads.
         */

        for (index = 0; index < NMICE; index++) {
   
                error = thread_fork("mouselock thread", 
                                    NULL, 
                                    index, 
                                    mouselock, 
                                    NULL
                                    );
      
                /*
                 * panic() on error.
                 */

                if (error) {
         
                        panic("mouselock: thread_fork failed: %s\n", 
                              strerror(error)
                              );
                }
        }
        // dispose sync primitives
        lock_destroy(dish_lock);
        cv_destroy(dish_cv);

        return 0;
}

/*
 * End of catlock.c
 */
