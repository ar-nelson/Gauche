/*
 * vm.h - Virtual machine
 *
 *  Copyright(C) 2000 by Shiro Kawai (shiro@acm.org)
 *
 *  Permission to use, copy, modify, ditribute this software and
 *  accompanying documentation for any purpose is hereby granted,
 *  provided that existing copyright notices are retained in all
 *  copies and that this notice is included verbatim in all
 *  distributions.
 *  This software is provided as is, without express or implied
 *  warranty.  In no circumstances the author(s) shall be liable
 *  for any damages arising out of the use of this software.
 *
 *  $Id: vm.h,v 1.10 2001-01-31 07:29:13 shiro Exp $
 */

#ifndef GAUCHE_VM_H
#define GAUCHE_VM_H

/* Local variable access:
 *   Regardless of frame allocation scheme, local variables are always
 *   accessed like this:
 *
 * (define (foo x y)
 *    ;; here, x is env->data[0], y is env->data[1]
 *    (let ((a ...) (b ...))
 *       ;; here, a is env->data[0], b is env->data[1],
 *       ;;       x is env->up->data[0], and so on.
 *       (lambda (r s)
 *          ;; here, r is env->data[0], s is env->data[1],
 *          ;;       a is env->up->data[0], x is env->up->up->data[0].
 *
 *     ....
 *
 * If frames are allocated on the stack first, they are copied to the
 * heap when a closure is created.   Once copied, it stays on the heap
 * until it's garbage collected.
 *
 * The toplevel frame is always a frame of size 1, containing the toplevel
 * module in data[0].   You can tell it's toplevel if env->up is NULL.
 */

typedef struct ScmEnvFrameRec {
    struct ScmEnvFrameRec *up;  /* static link */
    ScmObj info;                /* source code info for debug */
    int size;                   /* size of the frame */
    ScmObj data[1];             /* variable length */
} ScmEnvFrame;

#define ENV_HDR_SIZE   3        /* envframe header size */
#define ENV_SIZE(size)   ((size)+ENV_HDR_SIZE)

/*
 * Continuation
 *
 *  Continuation is represented as a chain of ScmContFrames.
 *  At non-tail procedure call, current environment, accumulated argument
 *  and program pointer are saved into the frame.  When procedure returns,
 *  Environment and program pointer is restored.  (Argument pointer is
 *  not restored by the normal procedure return, since the normal return
 *  may have pushed more arguments.  When a captured continuation is
 *  invoked, however, the argument pointer is restored.)
 *
 *  Capturing continuation is just a simple matter of saving the chain.
 */

typedef struct ScmContFrameRec {
    struct ScmContFrameRec *prev; /* previous frame */
    ScmEnvFrame *env;             /* current environment */
    ScmObj pc;                    /* next PC */
    int size;                     /* size of extra data */
} ScmContFrame;

#define CONT_FRAME_SIZE  4

extern void Scm_CallCC(ScmObj body);

/*
 * Source info
 *
 *   It is inserted in the compiled code by the compiler, and used
 *   by error handlers to obtain debugging information.  See compile.c
 *   for details.
 */

typedef struct ScmSourceInfoRec {
    SCM_HEADER;
    ScmObj info;
    struct ScmSourceInfoRec *up;
} ScmSourceInfo;

extern ScmClass Scm_SourceInfoClass;
#define SCM_CLASS_SOURCE_INFO    (&Scm_SourceInfoClass)

#define SCM_SOURCE_INFO(obj)     ((ScmSourceInfo*)(obj))
#define SCM_SOURCE_INFOP(obj)    SCM_XTYPEP(obj, SCM_CLASS_SOURCE_INFO)

ScmObj Scm_MakeSourceInfo(ScmObj info, ScmSourceInfo *up);

/*
 * C-level error handler
 */
typedef struct ScmErrorHandlerRec {
    struct ScmErrorHandlerRec *prev;
    jmp_buf jbuf;
} ScmErrorHandler;

/*
 * VM structure
 *
 *  Some of the VM "registers" are not in ScmVM, but declared as a
 *  local variable in the vm loop (run_loop).  They are only saved
 */

struct ScmVMRec {
    SCM_HEADER;
    ScmVM *parent;
    ScmModule *module;          /* current global namespace */
    ScmErrorHandler *escape;    /* current escape point */
    ScmObj errstr;              /* error string */
    ScmObj errorHandler;        /* error handler */
    
    int enableInline;           /* enable inlining on compilation */

    ScmPort *curin;             /* current input port */
    ScmPort *curout;            /* current output port */
    ScmPort *curerr;            /* current error port */

    ScmObj pc;                  /* program pointer. */
    ScmEnvFrame *env;           /* environment */
    ScmContFrame *cont;         /* continuation */
    ScmObj val0;

    ScmObj handlers;            /* chain of active dynamic handlers */

    ScmObj *sp;
    ScmObj *stack;
    int stackSize;
};

extern ScmVM *Scm_SetVM(ScmVM *vm);
extern ScmVM *Scm_NewVM(ScmVM *base, ScmModule *module);
extern void Scm_VMDump(ScmVM *vm);

extern ScmClass Scm_VMClass;
#define SCM_CLASS_VM              (&Scm_VMClass)

/*
 * VM instructions
 */
#define SCM_VM_INSN_TAG            0x0e

#define SCM_VM_INSNP(obj)            ((SCM_WORD(obj)&0x0f) == SCM_VM_INSN_TAG)
#define SCM_VM_INSN_CODE(obj)        ((SCM_WORD(obj)>>4)&0x0ff)
#define SCM_VM_INSN_ARG(obj)         (SCM_WORD(obj) >> 12)

#define SCM_VM_INSN_ARG0(obj)        ((SCM_WORD(obj) >> 12) & 0x03ff)
#define SCM_VM_INSN_ARG1(obj)        ((SCM_WORD(obj) >> 22) & 0x03ff)

#define SCM_VM_INSN(code) \
    SCM_OBJ(((code)<<4)|SCM_VM_INSN_TAG)
#define SCM_VM_INSN1(code, arg) \
    SCM_OBJ(((arg) << 12) | ((code) << 4) | SCM_VM_INSN_TAG)
#define SCM_VM_INSN2(code, arg0, arg1) \
    SCM_OBJ(((arg1) << 22) | ((arg0) << 12) | ((code) << 4) | SCM_VM_INSN_TAG)

enum {
#define DEFINSN(sym, nam, nparams)  sym,
#include "vminsn.h"
#undef DEFINSN
    SCM_VM_NUM_INSNS
};

extern int Scm__VMInsnWrite(ScmObj insn, ScmPort *port, int mode);
extern ScmObj Scm_VMInsnInspect(ScmObj obj);

/*
 * Error handling 
 */

#define SCM_PUSH_ERROR_HANDLER                    \
    do {                                          \
       ScmErrorHandler handler;                   \
       handler.prev = Scm_VM()->escape;           \
       Scm_VM()->escape = &handler;               \
       if (setjmp(handler.jbuf) == 0) {
           
#define SCM_WHEN_ERROR \
       } else {

#define SCM_POP_ERROR_HANDLER                     \
       }                                          \
       Scm_VM()->escape = Scm_VM()->escape->prev; \
    } while (0)

/*
 * C-continuation
 */

#define SCM_CCONT_DATA_SIZE 6

typedef struct ScmCContinuation {
    SCM_HEADER;
    ScmObj (*func)(ScmObj value, void **data);
    void *data[SCM_CCONT_DATA_SIZE];
} ScmCContinuation;

#define SCM_CCONT(obj)            ((ScmCContinuation*)(obj))
#define SCM_CCONTP(obj)           SCM_XTYPEP(obj, SCM_CLASS_CCONT)

extern ScmClass Scm_CContClass;
#define SCM_CLASS_CCONT           (&Scm_CContClass)

extern void Scm_VMPushCC(ScmObj (*func)(ScmObj value, void **data),
                         void **data,
                         int datasize);

/*
 * Compiler context
 */

enum {
    SCM_COMPILE_STMT,           /* Statement context.  The value of this
                                   expression will be discarded. */
    SCM_COMPILE_TAIL,           /* This is a tail expression. */
    SCM_COMPILE_NORMAL          /* Normal calling sequence. */
};

#include "vminline.h"

#endif /* GAUCHE_VM_H */
