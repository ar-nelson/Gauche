/*
 * module.c - module implementation
 *
 *  Copyright(C) 2000-2001 by Shiro Kawai (shiro@acm.org)
 *
 *  Permission to use, copy, modify, distribute this software and
 *  accompanying documentation for any purpose is hereby granted,
 *  provided that existing copyright notices are retained in all
 *  copies and that this notice is included verbatim in all
 *  distributions.
 *  This software is provided as is, without express or implied
 *  warranty.  In no circumstances the author(s) shall be liable
 *  for any damages arising out of the use of this software.
 *
 *  $Id: module.c,v 1.12 2001-03-05 10:45:32 shiro Exp $
 */

#include "gauche.h"

/*
 * Modules
 *
 *  A module maps symbols to global locations.
 *  Note that the mapping is resolved at the compile time.
 *  Scheme's current-module is therefore a syntax, instead of
 *  a procedure, to capture compile-time information.
 */

static int module_print(ScmObj obj, ScmPort *port, int mode)
{
    ScmModule *m = SCM_MODULE(obj);
    return Scm_Printf(port, "#<module %S>", m->name);
}

SCM_DEFCLASS(Scm_ModuleClass, "<module>", module_print,
             SCM_CLASS_COLLECTION_CPL);

static ScmHashTable *moduleTable; /* global, must be protected in MT env */

/*----------------------------------------------------------------------
 * Constructor
 */

/* internal */
static ScmObj make_module(ScmSymbol *name, ScmModule *parent)
{
    ScmModule *z;
    z = SCM_NEW(ScmModule);
    SCM_SET_CLASS(z, SCM_CLASS_MODULE);
    z->name = name;
    z->parent = parent;
    z->imported = SCM_NIL;
    z->exported = SCM_NIL;
    z->table = SCM_HASHTABLE(Scm_MakeHashTable(SCM_HASH_ADDRESS, NULL, 0));

    Scm_HashTablePut(moduleTable, SCM_OBJ(name), SCM_OBJ(z));
    return SCM_OBJ(z);
}

ScmObj Scm_MakeModule(ScmSymbol *name)
{
    return make_module(name, Scm_GaucheModule());
}

/*----------------------------------------------------------------------
 * Finding and modifying bindings
 */

ScmGloc *Scm_FindBinding(ScmModule *module, ScmSymbol *symbol,
                         int stay_in_module)
{
    ScmHashEntry *e = Scm_HashTableGet(module->table, SCM_OBJ(symbol));
    ScmModule *m;
    ScmObj p;

    if (e) return SCM_GLOC(e->value);
    if (!stay_in_module) {
        /* First, search from imported modules */
        SCM_FOR_EACH(p, module->imported) {
            SCM_ASSERT(SCM_MODULEP(SCM_CAR(p)));
            m = SCM_MODULE(SCM_CAR(p));
            e = Scm_HashTableGet(m->table, SCM_OBJ(symbol));
            if (e && !SCM_FALSEP(Scm_Memq(SCM_OBJ(symbol), m->exported)))
                return SCM_GLOC(e->value);
        }
        /* Then, search from parent module */
        for (m = module->parent; m; m = m->parent) {
            e = Scm_HashTableGet(m->table, SCM_OBJ(symbol));
            if (e) return SCM_GLOC(e->value);
        }
    }
    return NULL;
}

ScmObj Scm_SymbolValue(ScmModule *module, ScmSymbol *symbol)
{
    ScmObj mod;
    ScmGloc *g = Scm_FindBinding(module, symbol, FALSE);
    return (g != NULL)? g->value : SCM_UNBOUND;
}

ScmObj Scm_Define(ScmModule *module, ScmSymbol *symbol, ScmObj value)
{
    ScmGloc *g = Scm_FindBinding(module, symbol, TRUE);
    if (g) {
        g->value = value;
    } else {
        g = SCM_GLOC(Scm_MakeGloc(symbol, module));
        g->value = value;
        Scm_HashTablePut(module->table, SCM_OBJ(symbol), SCM_OBJ(g));
    }
    return SCM_OBJ(g);
}

ScmObj Scm_ImportModules(ScmModule *module, ScmObj list)
{
    ScmObj lp, head = SCM_NIL, tail;
    SCM_APPEND(head, tail, module->imported);
    SCM_FOR_EACH(lp, list) {
        if (!SCM_MODULEP(SCM_CAR(lp)))
            Scm_Error("module required, but got %S", SCM_CAR(lp));
        if (SCM_FALSEP(Scm_Memq(SCM_CAR(lp), head)))
            SCM_APPEND1(head, tail, SCM_CAR(lp));
    }
    return (module->imported = head);
}

ScmObj Scm_ExportSymbols(ScmModule *module, ScmObj list)
{
    ScmObj lp, syms = module->exported;
    SCM_FOR_EACH(lp, list) {
        if (!SCM_SYMBOLP(SCM_CAR(lp)))
            Scm_Error("symbol required, but got %S", SCM_CAR(lp));
        if (SCM_FALSEP(Scm_Memq(SCM_CAR(lp), syms)))
            syms = Scm_Cons(SCM_CAR(lp), syms);
    }
    return (module->exported = syms);
}

/*----------------------------------------------------------------------
 * Finding modules
 */

ScmObj Scm_FindModule(ScmSymbol *name)
{
    ScmHashEntry *e = Scm_HashTableGet(moduleTable, SCM_OBJ(name));
    if (e == NULL) return SCM_FALSE;
    else return e->value;
}

ScmObj Scm_AllModules(void)
{
    ScmObj h = SCM_NIL, t;
    ScmHashIter iter;
    ScmHashEntry *e;
    
    Scm_HashIterInit(moduleTable, &iter);
    while ((e = Scm_HashIterNext(&iter)) != NULL) {
        SCM_APPEND1(h, t, e->value);
    }
    return h;
}

void Scm_SelectModule(ScmModule *mod)
{
    SCM_ASSERT(SCM_MODULEP(mod));
    Scm_VM()->module = mod;
}

/*----------------------------------------------------------------------
 * Predefined modules and initialization
 */

static ScmModule *nullModule;
static ScmModule *schemeModule;
static ScmModule *gaucheModule;
static ScmModule *userModule;

ScmModule *Scm_NullModule(void)
{
    return nullModule;
}

ScmModule *Scm_SchemeModule(void)
{
    return schemeModule;
}

ScmModule *Scm_GaucheModule(void)
{
    return gaucheModule;
}

ScmModule *Scm_UserModule(void)
{
    return userModule;
}

ScmModule *Scm_CurrentModule(void)
{
    return Scm_VM()->module;
}

#define MAKEMOD(sym, parent) SCM_MODULE(make_module(SCM_SYMBOL(sym), parent))


void Scm__InitModule(void)
{
    moduleTable = SCM_HASHTABLE(Scm_MakeHashTable(SCM_HASH_ADDRESS, NULL, 64));

    nullModule   = MAKEMOD(SCM_SYM_NULL, NULL);
    schemeModule = MAKEMOD(SCM_SYM_SCHEME, nullModule);
    gaucheModule = MAKEMOD(SCM_SYM_GAUCHE, schemeModule);
    userModule   = MAKEMOD(SCM_SYM_USER, gaucheModule);
}

