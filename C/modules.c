/*************************************************************************
*									 *
*	 YAP Prolog 							 *
*									 *
*	Yap Prolog was developed at NCCUP - Universidade do Porto	 *
*									 *
* Copyright L.Damas, V.S.Costa and Universidade do Porto 1985-1997	 *
*									 *
**************************************************************************
*									 *
 File:		modules.c						 *
* Last rev:								 *
* mods:									 *
* comments:	module support						 *
*									 *
*************************************************************************/
#ifdef SCCS
static char     SccsId[] = "%W% %G%";
#endif

#include "Yap.h"
#include "Yatom.h"
#include "Heap.h"

STATIC_PROTO(Int p_current_module, (void));
STATIC_PROTO(Int p_current_module1, (void));


inline static ModEntry *
FetchModuleEntry(Atom at)
/* get predicate entry for ap/arity; create it if neccessary.              */
{
  Prop p0;
  AtomEntry *ae = RepAtom(at);

  WRITE_LOCK(ae->ARWLock);
  p0 = ae->PropsOfAE;
  while (p0) {
    ModEntry *me = RepModProp(p0);
    if ( me->KindOfPE == ModProperty
	 ) {
      WRITE_UNLOCK(ae->ARWLock);
      return me;
    }
    p0 = me->NextOfPE;
  }
  return NULL;
}

inline static ModEntry *
GetModuleEntry(Atom at)
/* get predicate entry for ap/arity; create it if neccessary.              */
{
  Prop p0;
  AtomEntry *ae = RepAtom(at);
  ModEntry *new;

  WRITE_LOCK(ae->ARWLock);
  p0 = ae->PropsOfAE;
  while (p0) {
    ModEntry *me = RepModProp(p0);
    if ( me->KindOfPE == ModProperty
	 ) {
      WRITE_UNLOCK(ae->ARWLock);
      return me;
    }
    p0 = me->NextOfPE;
  }
  new = (ModEntry *) Yap_AllocAtomSpace(sizeof(*new));
  INIT_RWLOCK(new->ModRWLock);
  new->KindOfPE = ModProperty;
  new->NextME = CurrentModules;
  CurrentModules = new;
  new->AtomOfME = ae;
  new->NextOfPE = ae->PropsOfAE;
  ae->PropsOfAE = AbsModProp(new);
  WRITE_UNLOCK(ae->ARWLock);
  return new;
}


#define ByteAdr(X) ((char *) &(X))
Term 
Yap_Module_Name(PredEntry *ap)
{
  Term mod;
  if (!ap->ModuleOfPred)
    /* If the system predicate is a metacall I should return the
       module for the metacall, which I will suppose has to be
       reachable from the current module anyway.

       So I will return the current module in case the system
       predicate is a meta-call. Otherwise it will still work.
    */
    mod =  CurrentModule;
  else {
    mod = ap->ModuleOfPred;
  }
  if (mod) return mod;
  return TermProlog;
}

static ModEntry * 
LookupModule(Term a)
{
  Atom at;

  /* prolog module */
  if (a == 0)
    return GetModuleEntry(AtomOfTerm(TermProlog));
  at = AtomOfTerm(a);
  return GetModuleEntry(at);
}

Term
Yap_Module(Term tmod)
{
  LookupModule(tmod);
  return tmod;
}

struct pred_entry *
Yap_ModulePred(Term mod)
{
  ModEntry *me;
  if (!(me = LookupModule(mod)))
    return NULL;
  return me->PredForME;
}

void
Yap_NewModulePred(Term mod, struct pred_entry *ap)
{
  ModEntry *me;

  if (!(me = LookupModule(mod)))
    return;
  /* LOCK THIS */
  ap->NextPredOfModule = me->PredForME;
  me->PredForME = ap;
}

static Int 
p_current_module(void)
{				/* $current_module(Old,New)		 */
  Term            t;
	
  if (CurrentModule) {
    if(!Yap_unify_constant(ARG1, CurrentModule))
      return FALSE;
  } else {
    if (!Yap_unify_constant(ARG1, TermProlog))
      return FALSE;
  }
  t = Deref(ARG2);
  if (IsVarTerm(t) || !IsAtomTerm(t))
    return FALSE;
  if (t == TermProlog) {
    CurrentModule = PROLOG_MODULE;
  } else {
    CurrentModule = t;
    LookupModule(CurrentModule);
  }
  return (TRUE);
}

static Int
p_current_module1(void)
{				/* $current_module(Old)		 */
  if (CurrentModule)
    return Yap_unify_constant(ARG1, CurrentModule);
  return Yap_unify_constant(ARG1, TermProlog);
}

static Int
p_change_module(void)
{				/* $change_module(New)		 */
  Term mod = Deref(ARG1);
  LookupModule(mod);
  CurrentModule = mod;
  return TRUE;
}

static Int 
cont_current_module(void)
{
  ModEntry  *imod = (ModEntry *)IntegerOfTerm(EXTRA_CBACK_ARG(1,1)), *next;
  Term t = MkAtomTerm(imod->AtomOfME);
  next = imod->NextME;

  /* ARG1 is unbound */
  Yap_unify(ARG1,t);
  if (!next)
    cut_succeed();
  EXTRA_CBACK_ARG(1,1) = MkIntegerTerm((Int)next);
  return TRUE;
}

static Int 
init_current_module(void)
{				/* current_module(?ModuleName)		 */
  Term t = Deref(ARG1);
  if (!IsVarTerm(t)) {
    if (!IsAtomTerm(t)) {
      Yap_Error(TYPE_ERROR_ATOM,t,"module name must be an atom");
      return FALSE;
    }
    return (FetchModuleEntry(AtomOfTerm(t)) != NULL);
  }
  EXTRA_CBACK_ARG(1,1) = MkIntegerTerm((Int)CurrentModules);
  return cont_current_module();
}

void 
Yap_InitModulesC(void)
{
  Yap_InitCPred("$current_module", 2, p_current_module, SafePredFlag|SyncPredFlag|HiddenPredFlag);
  Yap_InitCPred("$current_module", 1, p_current_module1, SafePredFlag|SyncPredFlag|HiddenPredFlag);
  Yap_InitCPred("$change_module", 1, p_change_module, SafePredFlag|SyncPredFlag|HiddenPredFlag);
  Yap_InitCPredBack("$all_current_modules", 1, 1, init_current_module, cont_current_module,
		SafePredFlag|SyncPredFlag|HiddenPredFlag);
}


void 
Yap_InitModules(void)
{
  LookupModule(MkAtomTerm(Yap_LookupAtom("prolog")));
  LookupModule(USER_MODULE);
  LookupModule(IDB_MODULE);
  LookupModule(ATTRIBUTES_MODULE);
  LookupModule(CHARSIO_MODULE);
  LookupModule(TERMS_MODULE);
  LookupModule(SYSTEM_MODULE);
  LookupModule(READUTIL_MODULE);
  LookupModule(HACKS_MODULE);
  LookupModule(GLOBALS_MODULE);
  CurrentModule = PROLOG_MODULE;
}
