/* 
 * stoplight.c
 *
 * 31-1-2003 : GWA : Stub functions created for CS161 Asst1.
 *
 * NB: You can use any synchronization primitives available to solve
 * the stoplight problem in this file.
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
 * Number of cars created.
 */

#define NCARS 20

#define TURN_LEFT 0
#define TURN_RIGHT 1
#define GO_STRAIGHT 2

#define NORTH 0
#define EAST 1
#define SOUTH 2
#define WEST 3

/*Synchronization primitives */
struct semaphore* NW_lock, *NE_lock, *SW_lock, *SE_lock;
struct semaphore* from_north, *from_south, *from_east, *from_west;
struct semaphore *join_sem;


/*
 *
 * Function Definitions
 *
 */

static const char *directions[] = { "N", "E", "S", "W" };

static const char *msgs[] = {
        "approaching:",
        "region1:    ",
        "region2:    ",
        "region3:    ",
        "leaving:    "
};

/* use these constants for the first parameter of message */
enum { APPROACHING, REGION1, REGION2, REGION3, LEAVING };

static void
message(int msg_nr, int carnumber, int cardirection, int destdirection)
{
        kprintf("%s car = %2d, direction = %s, destination = %s\n",
                msgs[msg_nr], carnumber,
                directions[cardirection], directions[destdirection]);
}
 
/*
 * gostraight()
 *
 * Arguments:
 *      unsigned long cardirection: the direction from which the car
 *              approaches the intersection.
 *      unsigned long carnumber: the car id number for printing purposes.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      This function should implement passing straight through the
 *      intersection from any direction.
 *      Write and comment this function.
 */

static
void
gostraight(unsigned long cardirection,
           unsigned long carnumber)
{
        /*
         * Avoid unused variable warnings.
         */
        
    if (cardirection == NORTH) { //0
        message(APPROACHING, carnumber, cardirection, SOUTH);

        P(NW_lock);
        V(from_north);
        message(REGION1, carnumber, cardirection, SOUTH);
        P(SW_lock);
        //once the car requires SW_lock,we release NW_lock.
        V(NW_lock);
        message(REGION2, carnumber, cardirection, SOUTH);
        message(LEAVING, carnumber, cardirection, SOUTH);
        V(SW_lock);

    } else if (cardirection == EAST) { //1
        message(APPROACHING, carnumber, cardirection, WEST);
        P(NE_lock);
        V(from_east);
        message(REGION1, carnumber, cardirection, WEST);
        P(NW_lock);
        V(NE_lock);
        message(REGION2, carnumber, cardirection, WEST);
        message(LEAVING, carnumber, cardirection, WEST);
        V(NW_lock);

    } else if (cardirection == SOUTH) { //2
        message(APPROACHING, carnumber, cardirection, NORTH);

        P(SE_lock);
        V(from_south);
        message(REGION1, carnumber, cardirection, NORTH);
        P(NE_lock);
        V(SE_lock);
        message(REGION2, carnumber, cardirection, NORTH);
        message(LEAVING, carnumber, cardirection, NORTH);
        V(NE_lock);

    } else {
        message(APPROACHING, carnumber, cardirection, EAST);

        assert(cardirection == WEST); //3
        P(SW_lock);
        V(from_west);
        message(REGION1, carnumber, cardirection, EAST);
        P(SE_lock);
        V(SW_lock);
        message(REGION2, carnumber, cardirection, EAST);
        message(LEAVING, carnumber, cardirection, EAST);
        V(SE_lock);
    }
}


/*
 * turnleft()
 *
 * Arguments:
 *      unsigned long cardirection: the direction from which the car
 *              approaches the intersection.
 *      unsigned long carnumber: the car id number for printing purposes.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      This function should implement making a left turn through the 
 *      intersection from any direction.
 *      Write and comment this function.
 */

static
void
turnleft(unsigned long cardirection,
         unsigned long carnumber)
{
        /*
         * Avoid unused variable warnings.
         */

        (void) cardirection;
        (void) carnumber;
    
    if (cardirection == NORTH) { //0
        message(APPROACHING, carnumber, cardirection, EAST);

        P(NW_lock);
        V(from_north);
        message(REGION1, carnumber, cardirection, EAST);
        P(SW_lock);
        V(NW_lock);
        message(REGION2, carnumber, cardirection, EAST);
        P(SE_lock);
        V(SW_lock);
        message(REGION3, carnumber, cardirection, EAST);
        message(LEAVING, carnumber, cardirection, EAST);
        V(SE_lock);
    } else if (cardirection == EAST) { //1
        message(APPROACHING, carnumber, cardirection, SOUTH);

        P(NE_lock);
        V(from_east);
        message(REGION1, carnumber, cardirection, SOUTH);
        P(NW_lock);
        V(NE_lock);
        message(REGION2, carnumber, cardirection, SOUTH);
        P(SW_lock);
        V(NW_lock);
        message(REGION3, carnumber, cardirection, SOUTH);
        message(LEAVING, carnumber, cardirection, SOUTH);
        V(SW_lock);

    } else if (cardirection == SOUTH) { //2
        message(APPROACHING, carnumber, cardirection, WEST);
        P(SE_lock);
        V(from_south);
        message(REGION1, carnumber, cardirection, WEST);
        P(NE_lock);
        V(SE_lock);
        message(REGION2, carnumber, cardirection, WEST);
        P(NW_lock);
        V(NE_lock);
        message(REGION3, carnumber, cardirection, WEST);
        message(LEAVING, carnumber, cardirection, WEST);
        V(NW_lock);

    } else {
        message(APPROACHING, carnumber, cardirection, NORTH);

        assert(cardirection == WEST); //3
        P(SW_lock);
        V(from_west);
        message(REGION1, carnumber, cardirection, NORTH);
        P(SE_lock);
        V(SW_lock);
        message(REGION2, carnumber, cardirection, NORTH);
        P(NE_lock);
        V(SE_lock);
        message(REGION3, carnumber, cardirection, NORTH);
        message(LEAVING, carnumber, cardirection, NORTH);
        V(NE_lock);
    }

}


/*
 * turnright()
 *
 * Arguments:
 *      unsigned long cardirection: the direction from which the car
 *              approaches the intersection.
 *      unsigned long carnumber: the car id number for printing purposes.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      This function should implement making a right turn through the 
 *      intersection from any direction.
 *      Write and comment this function.
 */

static
void
turnright(unsigned long cardirection,
          unsigned long carnumber)
{
    if (cardirection == NORTH) { //0
        message(APPROACHING, carnumber, cardirection, WEST);
        P(NW_lock);
        V(from_north);
        message(REGION1, carnumber, cardirection, WEST);
        message(LEAVING, carnumber, cardirection, WEST);
        V(NW_lock);
    } else if (cardirection == EAST) { //1
        message(APPROACHING, carnumber, cardirection, NORTH);
        P(NE_lock);
        V(from_east);
        message(REGION1, carnumber, cardirection, NORTH);
        message(LEAVING, carnumber, cardirection, NORTH);
        V(NE_lock);
    } else if (cardirection == SOUTH) { //2
        message(APPROACHING, carnumber, cardirection, EAST);
        P(SE_lock);
        V(from_south);
        message(REGION1, carnumber, cardirection, EAST);
        message(LEAVING, carnumber, cardirection, EAST);
        V(SE_lock);
    } else {
        message(APPROACHING, carnumber, cardirection, SOUTH);
        assert(cardirection == WEST); //3
        P(SW_lock);
        V(from_west);
        message(REGION1, carnumber, cardirection, SOUTH);
        message(LEAVING, carnumber, cardirection, SOUTH);
        V(SW_lock);
    }
}


/*
 * approachintersection()
 *
 * Arguments: 
 *      void * unusedpointer: currently unused.
 *      unsigned long carnumber: holds car id number.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      Change this function as necessary to implement your solution. These
 *      threads are created by createcars().  Each one must choose a direction
 *      randomly, approach the intersection, choose a turn randomly, and then
 *      complete that turn.  The code to choose a direction randomly is
 *      provided, the rest is left to you to implement.  Making a turn
 *      or going straight should be done by calling one of the functions
 *      above.
 */
 
static
void
approachintersection(void * unusedpointer,
                     unsigned long carnumber)
{
    int car_direction;
    int turn_direction;
        /*
         * Avoid unused variable and function warnings.
         */

    (void) unusedpointer;
    (void) carnumber;
	(void) gostraight;
	(void) turnleft;
	(void) turnright;

        /*
         * cardirection is set randomly.
         */

    car_direction = random() % 4;
    turn_direction = random() % 3;


    // we enforce the FIFO ordering for the cars approaching
    // the intersection from the same direction
    if (car_direction == 0) {// North
        // north
        P(from_north);
    } else if (car_direction == 1) { //East
        P(from_east);
    } else if (car_direction == 2) {//South
        P(from_south);
    } else {
        assert(car_direction == 3); //West
        P(from_west);
    }

    // Step 1: approaching an intersection
    // message(APPROACHING, carnumber, car_direction, turn_direction);

    // kprintf("car: %d, from: %c, turn_direction: %d\n", carnumber, directions[car_direction], turn_direction);
    // after we approached the intersection, we enter it 
    if (turn_direction == TURN_LEFT) { //0
        turnleft(car_direction, carnumber);
    } else if (turn_direction == TURN_RIGHT) { //1
        turnright(car_direction, carnumber);
    } else {
        assert(turn_direction == GO_STRAIGHT); //2
        gostraight(car_direction, carnumber);
    }


    V(join_sem);
}


/*
 * createcars()
 *
 * Arguments:
 *      int nargs: unused.
 *      char ** args: unused.
 *
 * Returns:
 *      0 on success.
 *
 * Notes:
 *      Driver code to start up the approachintersection() threads.  You are
 *      free to modiy this code as necessary for your solution.
 */

int
createcars(int nargs,
           char ** args)
{
        int index, error;

        /*
         * Avoid unused variable warnings.
         */

        (void) nargs;
        (void) args;
        
        NW_lock = sem_create("NW_lock",1); 
        NE_lock = sem_create("NE_lock",1);
        SW_lock = sem_create("SW_lock",1);
        SE_lock = sem_create("SE_lock",1);
        // kprintf("before sem_create !\n");
        from_north = sem_create("sem_north", 1);
        from_south = sem_create("sem_south", 1);
        from_east = sem_create("sem_east", 1);
        from_west = sem_create("sem_west", 1);
        // kprintf("we created sems\n");
        join_sem = sem_create("join_sem", 0);


        /*
         * Start NCARS approachintersection() threads.
         */

        for (index = 0; index < NCARS; index++) {

                error = thread_fork("approachintersection thread",
                                    NULL,
                                    index,
                                    approachintersection,
                                    NULL
                                    );

                /*
                 * panic() on error.
                 */

                if (error) {
                        
                        panic("approachintersection: thread_fork failed: %s\n",
                              strerror(error)
                              );
                }

        }

        int i ;
        for (i = 0; i < NCARS; i++) {
            P(join_sem);
        }
                sem_destroy(NW_lock);
                sem_destroy(NE_lock);
                sem_destroy(SW_lock);
                sem_destroy(SE_lock);
                sem_destroy(from_north);
                sem_destroy(from_south);
                sem_destroy(from_east);
                sem_destroy(from_west);
                sem_destroy(join_sem);
        return 0;
}
