#ifndef TCLTALK_PT_H
#define TCLTALK_PT_H

// Protothreads by Adam Dunkels
// Extremely lightweight, stackless local continuations

typedef struct pt {
    unsigned short lc;
} pt;

#define PT_WAITING 0
#define PT_YIELDED 1
#define PT_EXITED  2
#define PT_ENDED   3

#define PT_INIT(pt)          ((pt)->lc = 0)
#define PT_BEGIN(pt)         switch((pt)->lc) { case 0:
#define PT_YIELD(pt)         do { (pt)->lc = __LINE__; return PT_YIELDED; case __LINE__:; } while(0)
#define PT_WAIT_UNTIL(pt, c) do { (pt)->lc = __LINE__; case __LINE__: if(!(c)) return PT_WAITING; } while(0)
#define PT_END(pt)           } (pt)->lc = 0; return PT_ENDED;

#endif // TCLTALK_PT_H
