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
* File:		dbase.c							 *
* Last rev:	8/2/88							 *
* mods:									 *
* comments:	YAP's internal data base				 *
*									 *
*************************************************************************/
#ifdef SCCS
static char     SccsId[] = "%W% %G%";
#endif

#include "Yap.h"
#include "clause.h"
#include "yapio.h"
#include "heapgc.h"
#if HAVE_STRING_H
#include <string.h>
#endif
#if HAVE_STRING_H
#include <string.h>
#endif
#include <stdlib.h>

/* There are two options to implement traditional immediate update semantics.

   - In the first option, we only remove an element of the chain when
   it is phisically disposed of. This simplifies things, because
   pointers are always valid, but it complicates some stuff a bit:

   o You may have go through long lines of deleted db entries before you 
   actually reach the one you want.

   o Deleted clauses are also not removed of the chain. The solution
   was to place a fail in every clause, but you still have to
   backtrack through failed clauses.

   An alternative solution is to remove clauses from the chain, even
   if they are still phisically present. Unfortunately this creates
   problems because immediate update semantics means you have to
   backtrack clauses or see the db entries stored later. 

   There are several solutions. One of the simplest is to use an age
   counter. When you backtrack to a removed clause or to a deleted db
   entry you use the age to find newly entered clauses in the DB.

   This still causes a problem when you backtrack to a deleted
   clause, because clauses are supposed to point to the next
   alternative, and having been removed from the chain you cannot
   point there directly. One solution is to have a predicate in C that 
   recovers the place where to go to and then gets rid of the clause.

*/


#define DISCONNECT_OLD_ENTRIES 1

#ifdef MACYAPBUG
#define Register
#else
#define Register	register
#endif

/* Flags for recorda or recordz				 */
/* MkCode should be the same as CodeDBProperty */
#define MkFirst	1
#define MkCode  CodeDBBit
#define MkLast	4
#define WithRef	8
#define MkIfNot	16
#define InQueue	32

#define FrstDBRef(V)	( (V) -> First )
#define NextDBRef(V)	( (V) -> Next )

#define DBLength(V)	(sizeof(DBStruct) + (Int)(V) + CellSize)
#define AllocDBSpace(V)	((DBRef)Yap_AllocCodeSpace(V))
#define FreeDBSpace(V)	Yap_FreeCodeSpace(V)

#if SIZEOF_INT_P==4
#define ToSmall(V)	((link_entry)(Unsigned(V)>>2))
#else
#define ToSmall(V)	((link_entry)(Unsigned(V)>>3))
#endif

#define DEAD_REF(ref) FALSE

#ifdef SFUNC

#define MaxSFs		256

typedef struct {
  Term            SName;	/* The culprit */
  CELL           *SFather;      /* and his father's position */
}               SFKeep;
#endif

typedef struct queue_entry {
  struct queue_entry *next;
  DBTerm *DBT;
} QueueEntry;

typedef struct idb_queue
{
  Functor id;		/* identify this as being pointed to by a DBRef */
  SMALLUNSGN    Flags;  /* always required */
#if defined(YAPOR) || defined(THREADS)
  rwlock_t    QRWLock;         /* a simple lock to protect this entry */
#endif
  QueueEntry *FirstInQueue, *LastInQueue;
}  db_queue;

#define HashFieldMask		((CELL)0xffL)
#define DualHashFieldMask	((CELL)0xffffL)
#define TripleHashFieldMask	((CELL)0xffffffL)
#define FourHashFieldMask	((CELL)0xffffffffL)

#define    ONE_FIELD_SHIFT         8
#define   TWO_FIELDS_SHIFT        16
#define THREE_FIELDS_SHIFT        24

#define AtomHash(t)	(Unsigned(t)>>4)
#define FunctorHash(t)  (Unsigned(t)>>4)
#define NumberHash(t)   (Unsigned(IntOfTerm(t)))

#define LARGE_IDB_LINK_TABLE 1

/* traditionally, YAP used a link table to recover IDB terms*/
#define IDB_LINK_TABLE 1
#if LARGE_IDB_LINK_TABLE
typedef BITS32 link_entry; 
#define SIZEOF_LINK_ENTRY 4
#else
typedef BITS16 link_entry; 
#define SIZEOF_LINK_ENTRY 2
#endif
/* a second alternative is to just use a tag */
/*#define IDB_USE_MBIT 1*/

/* These global variables are necessary to build the data base
   structure */
typedef struct db_globs {
#ifdef IDB_LINK_TABLE
  link_entry  *lr, *LinkAr;
#endif
/* we cannot call Error directly from within recorded(). These flags are used
   to delay for a while
*/
  DBRef    *tofref;	/* place the refs also up	 */
#ifdef SFUNC
  CELL    *FathersPlace;	/* Where the father was going when the term
				 * was reached */
  SFKeep  *SFAr, *TopSF;	/* Where are we putting our SFunctors */
#endif
  DBRef    found_one;	/* Place where we started recording */
} dbglobs;

static dbglobs *s_dbg;

#ifdef SUPPORT_HASH_TABLES
typedef struct {
  CELL  key;
  DBRef entry;
} hash_db_entry;

typedef table {
  Int NOfEntries;
  Int HashArg;
  hash_db_entry *table;
} hash_db_table;
#endif

STATIC_PROTO(CELL *cpcells,(CELL *,CELL*,Int));
#ifdef IDB_LINK_TABLE
STATIC_PROTO(void linkblk,(link_entry *,CELL *,CELL));
#endif
#ifdef IDB_USE_MBIT
STATIC_PROTO(CELL *linkcells,(CELL *,Int));
#endif
STATIC_PROTO(Int cmpclls,(CELL *,CELL *,Int));
STATIC_PROTO(Prop FindDBProp,(AtomEntry *, int, unsigned int, Term));
STATIC_PROTO(CELL  CalcKey, (Term));
#ifdef COROUTINING
STATIC_PROTO(CELL  *MkDBTerm, (CELL *, CELL *, CELL *, CELL *, CELL *, CELL *,int *, struct db_globs *));
#else
STATIC_PROTO(CELL  *MkDBTerm, (CELL *, CELL *, CELL *, CELL *, CELL *, int *, struct db_globs *));
#endif
STATIC_PROTO(DBRef  CreateDBStruct, (Term, DBProp, int, int *, UInt, struct db_globs *));
STATIC_PROTO(DBRef  record, (int, Term, Term, Term));
STATIC_PROTO(DBRef  check_if_cons, (DBRef, Term));
STATIC_PROTO(DBRef  check_if_var, (DBRef));
STATIC_PROTO(DBRef  check_if_wvars, (DBRef, unsigned int, CELL *));
#ifdef IDB_LINK_TABLE
STATIC_PROTO(int  scheckcells, (int, CELL *, CELL *, link_entry *, CELL));
#endif
STATIC_PROTO(DBRef  check_if_nvars, (DBRef, unsigned int, CELL *, struct db_globs *));
STATIC_PROTO(Int  p_rcda, (void));
STATIC_PROTO(Int  p_rcdap, (void));
STATIC_PROTO(Int  p_rcdz, (void));
STATIC_PROTO(Int  p_rcdzp, (void));
STATIC_PROTO(Int  p_drcdap, (void));
STATIC_PROTO(Int  p_drcdzp, (void));
STATIC_PROTO(Term  GetDBTerm, (DBTerm *));
STATIC_PROTO(DBProp  FetchDBPropFromKey, (Term, int, int, char *));
STATIC_PROTO(Int  i_recorded, (DBProp,Term));
STATIC_PROTO(Int  c_recorded, (int));
STATIC_PROTO(Int  co_rded, (void));
STATIC_PROTO(Int  in_rdedp, (void));
STATIC_PROTO(Int  co_rdedp, (void));
STATIC_PROTO(Int  p_first_instance, (void));
STATIC_PROTO(void  ErasePendingRefs, (DBTerm *));
STATIC_PROTO(void  RemoveDBEntry, (DBRef));
STATIC_PROTO(void  EraseLogUpdCl, (LogUpdClause *));
STATIC_PROTO(void  MyEraseClause, (DynamicClause *));
STATIC_PROTO(void  PrepareToEraseClause, (DynamicClause *, DBRef));
STATIC_PROTO(void  EraseEntry, (DBRef));
STATIC_PROTO(Int  p_erase, (void));
STATIC_PROTO(Int  p_eraseall, (void));
STATIC_PROTO(Int  p_erased, (void));
STATIC_PROTO(Int  p_instance, (void));
STATIC_PROTO(int  NotActiveDB, (DBRef));
STATIC_PROTO(DBEntry  *NextDBProp, (PropEntry *));
STATIC_PROTO(Int  init_current_key, (void));
STATIC_PROTO(Int  cont_current_key, (void));
STATIC_PROTO(Int  cont_current_key_integer, (void));
STATIC_PROTO(Int  p_rcdstatp, (void));
STATIC_PROTO(Int  p_somercdedp, (void));
STATIC_PROTO(yamop * find_next_clause, (DBRef));
STATIC_PROTO(Int  p_jump_to_next_dynamic_clause, (void));
#ifdef SFUNC
STATIC_PROTO(void  SFVarIn, (Term));
STATIC_PROTO(void  sf_include, (SFKeep *));
#endif
STATIC_PROTO(Int  p_init_queue, (void));
STATIC_PROTO(Int  p_enqueue, (void));
STATIC_PROTO(void keepdbrefs, (DBTerm *));
STATIC_PROTO(Int  p_dequeue, (void));
STATIC_PROTO(void ErDBE, (DBRef));
STATIC_PROTO(void ReleaseTermFromDB, (DBTerm *));
STATIC_PROTO(PredEntry *new_lu_entry, (Term));
STATIC_PROTO(PredEntry *new_lu_int_key, (Int));
STATIC_PROTO(PredEntry *find_lu_entry, (Term));
STATIC_PROTO(DBProp find_int_key, (Int));

#if OS_HANDLES_TR_OVERFLOW
#define db_check_trail(x)
#elif USE_SYSTEM_MALLOC
#define db_check_trail(x) {                            \
  if (Unsigned(dbg->tofref) == Unsigned(x)) {          \
    goto error_tr_overflow;                            \
  }                                                    \
}
#else
#define db_check_trail(x) {                            \
  if (Unsigned(dbg->tofref) == Unsigned(x)) {          \
    if(!Yap_growtrail (sizeof(CELL) * 16 * 1024L)) {   \
      goto error_tr_overflow;                          \
    }                                                  \
  }                                                    \
}
#endif


#ifdef SUPPORT_HASH_TABLES
/* related property and hint on number of entries */
static void create_hash_table(DBProp p, Int hint) {
  int off = sizeof(CELL)*4, out;
  Int size;

  if (hint < p->NOfEntries)
    hint = p->NOfEntries;
  while (off) {
    int limit = 1 << (off);
    if (inp >= limit) {
      out += off;
      inp >>= off;
    }
    off >>= 1;
  }
  if ((size = 1 << out) < hint)
    hint <<= 1;
  /* clean up the table */
  pt = tbl = (hash_db_entry *)AllocDBSpace(hint*sizeof(hash_db_entry));
  for (i=0; i< hint; i++) {
    pt->key = NULL;
    pt++;
  }
  /* next insert the entries */
}

static void insert_in_table() {
  
}

static void remove_from_table() {
  
}
#endif

#ifdef IDB_LINK_TABLE
inline static CELL *cpcells(CELL *to, CELL *from, Int n)
{
#if HAVE_MEMMOVE
  memmove((void *)to, (void *)from, (size_t)(n*sizeof(CELL)));
  return(to+n);
#else
  while (n-- >= 0) {
    *to++ = *from++;
  }
  return(to);
#endif
}

static void linkblk(link_entry *r, CELL *c, CELL offs)
{
  CELL p;

  while ((p = (CELL)*r) != 0) {
    Term t = c[p];
    r++;
    c[p] = AdjustIDBPtr(t, offs);
  }
}
#endif

#ifdef IDB_USE_MBIT
inline static CELL *cpcells(register CELL *to, register CELL *from, Int n)
{
  CELL *last = to + n;
  register CELL off = ((CELL)to)-MBIT;
  while (to <= last) {
    register d0 = *from++;
    if (MARKED(d0)) 
      *to++ = AdjustIDBPtr(d0, off);
    else
      *to++ = d0;
  }
  return(to);
}

static CELL *linkcells(register CELL *to, Int n)
{
  CELL *last = to + n;
  register CELL off = ((CELL)to)-MBIT;
  while(to <= last) {
    register d0 = *to++;
    if (MARKED(d0)) 
      to[-1] = AdjustIDBPtr(d0, off);
  }
  return(to);
}


#endif

static Int cmpclls(CELL *a,CELL *b,Int n)
{
  while (n-- > 0) {
    if(*a++ != *b++) return FALSE;
  }
  return TRUE;
}

#if !THREADS
int Yap_DBTrailOverflow()
{
#ifdef IDB_USE_MBIT
  return(FALSE);
#endif
#ifdef IDB_LINK_TABLE
  return((CELL *)s_dbg->lr > (CELL *)s_dbg->tofref - 2048);
#endif
}
#endif

/* get DB entry for ap/arity; */
static Prop 
FindDBPropHavingLock(AtomEntry *ae, int CodeDB, unsigned int arity, Term dbmod)
{
  Prop          p0;
  DBProp        p;

  p = RepDBProp(p0 = ae->PropsOfAE);
  while (p0 && (((p->KindOfPE & ~0x1) != (CodeDB|DBProperty)) ||
		(p->ArityOfDB != arity) ||
		((CodeDB & MkCode) && p->ModuleOfDB && p->ModuleOfDB != dbmod))) {
    p = RepDBProp(p0 = p->NextOfPE);
  }
  return (p0);
}


/* get DB entry for ap/arity; */
static Prop 
FindDBProp(AtomEntry *ae, int CodeDB, unsigned int arity, Term dbmod)
{
  Prop out;

  READ_LOCK(ae->ARWLock);
  out = FindDBPropHavingLock(ae, CodeDB, arity, dbmod);
  READ_UNLOCK(ae->ARWLock);
  return(out);
}



/* These two functions allow us a fast lookup method in the data base */
/* PutMasks builds the mask and hash for a single argument	 */
inline static CELL 
CalcKey(Term tw)
{
  /* The first argument is known to be instantiated */
  if (IsApplTerm(tw)) {
    Functor f = FunctorOfTerm(tw);
    if (IsExtensionFunctor(f)) {
      if (f == FunctorDBRef) {
	return(FunctorHash(tw));	/* Ref */
      } /* if (f == FunctorLongInt || f == FunctorDouble) */
      return(NumberHash(RepAppl(tw)[1]));
    }
    return(FunctorHash(f));
  } else if (IsAtomOrIntTerm(tw)) {
    if (IsAtomTerm(tw)) {
      return(AtomHash(tw));
    }
    return(NumberHash(tw));
  }
  return(FunctorHash(FunctorList));
}

/* EvalMasks builds the mask and hash for up to three arguments of a term */
static CELL 
EvalMasks(register Term tm, CELL *keyp)
{

  if (IsVarTerm(tm)) {
    *keyp = 0L;
    return(0L);
  } else if (IsApplTerm(tm)) {
    Functor         fun = FunctorOfTerm(tm);

    if (IsExtensionFunctor(fun)) {
      if (fun == FunctorDBRef) {
	*keyp = FunctorHash(tm);	/* Ref */
      } else /* if (f == FunctorLongInt || f == FunctorDouble) */ {
	*keyp = NumberHash(RepAppl(tm)[1]);
      }
      return(FourHashFieldMask);
    } else {
      unsigned int    arity;

      arity = ArityOfFunctor(fun);
#ifdef SFUNC
      if (arity == SFArity) {	/* do not even try to calculate masks */
	*keyp = key;
	return(FourHashFieldMask);
      }
#endif
      switch (arity) {
      case 1:
	{
	  Term tw = ArgOfTerm(1, tm);

	  if (IsNonVarTerm(tw)) {
	    *keyp = (FunctorHash(fun) & DualHashFieldMask) | (CalcKey(tw) << TWO_FIELDS_SHIFT);
	    return(FourHashFieldMask);
	  } else {
	    *keyp = (FunctorHash(fun) & DualHashFieldMask);
	    return(DualHashFieldMask);
	  }
	}
      case 2:
	{
	  Term tw1, tw2;
	  CELL key, mask;

	  key = FunctorHash(fun) & DualHashFieldMask;
	  mask = DualHashFieldMask;

	  tw1 = ArgOfTerm(1, tm);
	  if (IsNonVarTerm(tw1)) {
	    key |= ((CalcKey(tw1) & HashFieldMask) << TWO_FIELDS_SHIFT);
	    mask |= (HashFieldMask << TWO_FIELDS_SHIFT);
	  }
	  tw2 = ArgOfTerm(2, tm);
	  if (IsNonVarTerm(tw2)) {
	    *keyp = key | (CalcKey(tw2) << THREE_FIELDS_SHIFT);
	    return(mask | (HashFieldMask << THREE_FIELDS_SHIFT));
	  } else {
	    *keyp = key;
	    return(mask);
	  }
	}
      default:
	{
	  Term tw1, tw2, tw3;
	  CELL key, mask;

	  key = FunctorHash(fun)  & HashFieldMask;
	  mask = HashFieldMask;

	  tw1 = ArgOfTerm(1, tm);
	  if (IsNonVarTerm(tw1)) {
	    key |= (CalcKey(tw1) & HashFieldMask) << ONE_FIELD_SHIFT;
	    mask |= HashFieldMask << ONE_FIELD_SHIFT;
	  }
	  tw2 = ArgOfTerm(2, tm);
	  if (IsNonVarTerm(tw2)) {
	    key |= (CalcKey(tw2) & HashFieldMask) << TWO_FIELDS_SHIFT;
	    mask |= HashFieldMask << TWO_FIELDS_SHIFT;
	  }
	  tw3 = ArgOfTerm(3, tm);
	  if (IsNonVarTerm(tw3)) {
	    *keyp = key | (CalcKey(tw3) << THREE_FIELDS_SHIFT);
	    return(mask | (HashFieldMask << THREE_FIELDS_SHIFT));
	  } else {
	    *keyp = key;
	    return(mask);
	  }
	}
      }
    }
  } else {
    CELL key  = (FunctorHash(FunctorList) & DualHashFieldMask);
    CELL mask = DualHashFieldMask;
    Term th = HeadOfTerm(tm), tt;

    if (IsNonVarTerm(th)) {
      mask |= (HashFieldMask << TWO_FIELDS_SHIFT);
      key |= (CalcKey(th) << TWO_FIELDS_SHIFT);
    }
    tt = TailOfTerm(tm);
    if (IsNonVarTerm(tt)) {
      *keyp = key | (CalcKey(tt) << THREE_FIELDS_SHIFT);
      return( mask|(HashFieldMask << THREE_FIELDS_SHIFT));
    }
    *keyp = key;
    return(mask);
  }
}

CELL 
Yap_EvalMasks(register Term tm, CELL *keyp)
{
  return EvalMasks(tm, keyp);
}


/* Called to inform that a new pointer to a data base entry has been added */
#define MarkThisRef(Ref)	((Ref)->NOfRefsTo ++ )

/* From a term, builds its representation in the data base */

/* otherwise, we just need to restore variables*/
typedef struct {
  CELL *addr;
} visitel;
#define DB_UNWIND_CUNIF()                                        \
         while (visited < (visitel *)AuxSp) {                 \
            RESET_VARIABLE(visited->addr);                    \
            visited ++;                                       \
         }

/* no checking for overflow while building DB terms yet */
#define  CheckDBOverflow(X) if (CodeMax+X >= (CELL *)visited-1024) {     \
    goto error;					                      \
   }
    
/* no checking for overflow while building DB terms yet */
#define  CheckVisitOverflow() if ((CELL *)to_visit+1024 >= ASP) {     \
    goto error2;					              \
   }
    
static CELL *
copy_long_int(CELL *st, CELL *pt)
{
  /* first thing, store a link to the list before we move on */
  st[0] = (CELL)FunctorLongInt;
  st[1] = pt[1];
  st[2] = ((2*sizeof(CELL)+EndSpecials)|MBIT);
  /* now reserve space */
  return st+3;
}

static CELL *
copy_double(CELL *st, CELL *pt)
{
  /* first thing, store a link to the list before we move on */
  st[0] = (CELL)FunctorDouble;
  st[1] = pt[1];
#if  SIZEOF_DOUBLE == 2*SIZEOF_LONG_INT
  st[2] = pt[2];
  st[3] = ((3*sizeof(CELL)+EndSpecials)|MBIT);
#else
  st[2] = ((2*sizeof(CELL)+EndSpecials)|MBIT);
#endif
  /* now reserve space */
  return st+(2+SIZEOF_DOUBLE/SIZEOF_LONG_INT);
}

#ifdef USE_GMP
static CELL *
copy_big_int(CELL *st, CELL *pt)
{
  Int sz = 
    sizeof(MP_INT)+
    (((MP_INT *)(pt+1))->_mp_alloc*sizeof(mp_limb_t));

  /* first functor */
  st[0] = (CELL)FunctorBigInt;
  /* then the actual number */
  memcpy((void *)(st+1), (void *)(pt+1), sz);
  st = st+1+sz/CellSize;
  /* then the tail for gc */ 
  st[0] = (sz+CellSize+EndSpecials)|MBIT;
  return st+1;
}
#endif /* BIG_INT */

#define DB_MARKED(d0) ((CELL *)(d0) < CodeMax && (CELL *)(d0) >= tbase)


/* This routine creates a complex term in the heap. */
static CELL *MkDBTerm(register CELL *pt0, register CELL *pt0_end,
		     register CELL *StoPoint,
		     CELL *CodeMax, CELL *tbase,
#ifdef COROUTINING
		     CELL *attachmentsp,
#endif
		     int *vars_foundp,
		     struct db_globs *dbg)
{

#if THREADS
#undef Yap_REGS
  register REGSTORE *regp = Yap_regp;
#define Yap_REGS (*regp)
#endif
  register visitel *visited = (visitel *)AuxSp;
  /* store this in H */
  register CELL **to_visit = (CELL **)H;
  CELL **to_visit_base = to_visit;
  /* where we are going to add a new pair */
  int vars_found = 0;
#ifdef COROUTINING
  Term ConstraintsTerm = TermNil;
  CELL *origH = H;
#endif
  CELL *CodeMaxBase = CodeMax;

 loop:
  while (pt0 <= pt0_end) {

    CELL *ptd0 = pt0;
    CELL d0 = *ptd0;
  restart:
    if (IsVarTerm(d0))
      goto deref_var; 

    if (IsApplTerm(d0)) {
      register Functor f;
      register CELL *ap2;
      
      /* we will need to link afterwards */
      ap2 = RepAppl(d0);
#ifdef RATIONAL_TREES
      if (ap2 >= tbase && ap2 < StoPoint) {
	*dbg->lr++ = ToSmall((CELL)(StoPoint)-(CELL)(tbase));
	db_check_trail(dbg->lr);
	*StoPoint++ = d0;
	++pt0;
	continue;
      }
#endif
#ifdef IDB_LINK_TABLE
      *dbg->lr++ = ToSmall((CELL)(StoPoint)-(CELL)(tbase));
      db_check_trail(dbg->lr);
#endif
      f = (Functor)(*ap2);
      if (IsExtensionFunctor(f)) {
	switch((CELL)f) {
	case (CELL)FunctorDBRef:
	  {
	    DBRef dbentry;
	    /* store now the correct entry */
	    dbentry = DBRefOfTerm(d0);
	    *StoPoint++ = d0;
#ifdef IDB_LINK_TABLE
	    dbg->lr--;
#endif
	    if (!(dbentry->Flags & StaticMask)) {
	      if (dbentry->Flags & LogUpdMask) {
		LogUpdClause *cl = (LogUpdClause *)dbentry;

		cl->ClRefCount++;
	      } else {
		dbentry->NOfRefsTo++;
	      }
	    }
	    *--dbg->tofref = dbentry;
	    db_check_trail(dbg->lr);
	    /* just continue the loop */
	    ++ pt0;
	    continue;
	  }
	case (CELL)FunctorLongInt:
#ifdef IDB_USE_MBIT
	  *StoPoint++ = AbsAppl(CodeMax)|MBIT;
#else
	  *StoPoint++ = AbsAppl(CodeMax);
#endif
	  CheckDBOverflow(3);
	  CodeMax = copy_long_int(CodeMax, ap2);
	  ++pt0;
	  continue;
#ifdef USE_GMP
	case (CELL)FunctorBigInt:
	  CheckDBOverflow(3);
	  /* first thing, store a link to the list before we move on */
#ifdef IDB_USE_MBIT
	  *StoPoint++ = AbsAppl(CodeMax)|MBIT;
#else
	  *StoPoint++ = AbsAppl(CodeMax);
#endif
	  CodeMax = copy_big_int(CodeMax, ap2);
	  ++pt0;
	  continue;
#endif
	case (CELL)FunctorDouble:
	  {
	    CELL *st = CodeMax;

	    CheckDBOverflow(4);
	    /* first thing, store a link to the list before we move on */
#ifdef IDB_USE_MBIT
	    *StoPoint++ = AbsAppl(st)|MBIT;
#else
	    *StoPoint++ = AbsAppl(st);
#endif
	    CodeMax = copy_double(CodeMax, ap2);
	    ++pt0;
	    continue;
	  }
	}
      }
      /* first thing, store a link to the list before we move on */
#ifdef IDB_USE_MBIT
      *StoPoint++ = AbsAppl(CodeMax)|MBIT;
#else
      *StoPoint++ = AbsAppl(CodeMax);
#endif
      /* next, postpone analysis to the rest of the current list */
#ifdef RATIONAL_TREES
      to_visit[0] = pt0+1;
      to_visit[1] = pt0_end;
      to_visit[2] = StoPoint;
      to_visit[3] = (CELL *)*pt0;
      to_visit += 4;
      *pt0 = StoPoint[-1];
#else
      if (pt0 < pt0_end) {
	to_visit[0] = pt0+1;
	to_visit[1] = pt0_end;
	to_visit[2] = StoPoint;
	to_visit += 3;
      }
#endif
      CheckVisitOverflow();
      d0 = ArityOfFunctor(f);
      pt0 = ap2+1;
      pt0_end = ap2 + d0;
      /* prepare for our new compound term */
      /* first the functor */
      CheckDBOverflow(d0);
      *CodeMax++ = (CELL)f;
      /* we'll be working here */
      StoPoint = CodeMax;
      /* now reserve space */
      CodeMax += d0;
      continue;
    }
    else if (IsPairTerm(d0)) {
      /* we will need to link afterwards */
#ifdef RATIONAL_TREES
      CELL *ap2 = RepPair(d0);
      if (ap2 >= tbase && ap2 < StoPoint) {
	*StoPoint++ = d0;
	*dbg->lr++ = ToSmall((CELL)(StoPoint)-(CELL)(tbase));
	db_check_trail(dbg->lr);
	++pt0;
	continue;
      }
#endif
#ifdef IDB_LINK_TABLE
      *dbg->lr++ = ToSmall((CELL)(StoPoint)-(CELL)(tbase));
      db_check_trail(dbg->lr);
#endif
#ifdef IDB_USE_MBIT
      *StoPoint++ =
	AbsPair(CodeMax)|MBIT;
#else
      *StoPoint++ = AbsPair(CodeMax);
#endif
      /* next, postpone analysis to the rest of the current list */
#ifdef RATIONAL_TREES
      to_visit[0] = pt0+1;
      to_visit[1] = pt0_end;
      to_visit[2] = StoPoint;
      to_visit[3] = (CELL *)*pt0;
      to_visit += 4;
      *pt0 = StoPoint[-1];
#else
      if (pt0 < pt0_end) {
	to_visit[0] = pt0+1;
	to_visit[1] = pt0_end;
	to_visit[2] = StoPoint;
	to_visit += 3;
      }
#endif
      CheckVisitOverflow();
      /* new list */
      /* we are working at CodeMax */
      StoPoint = CodeMax;
      /* set ptr to new term being analysed */
      pt0 = RepPair(d0);
      pt0_end = RepPair(d0) + 1;
      /* reserve space for our new list */
      CodeMax += 2;
      CheckDBOverflow(2);
      continue;
    } else if (IsAtomOrIntTerm(d0)) {
      *StoPoint++ = d0;
      ++pt0;
      continue;
    }
    
    /* the code to dereference a  variable */
  deref_var:
    if (!DB_MARKED(d0)) {
      if ( 
#if SBA
	  d0 != 0
#else
	  d0 != (CELL)ptd0
#endif
	  ) {
	ptd0 = (Term *) d0;
	d0 = *ptd0;
	goto restart; /* continue dereferencing */
      }
      /* else just drop to found_var */
    }
    /* else just drop to found_var */
    {
      CELL displacement = (CELL)(StoPoint)-(CELL)(tbase);
      
      pt0++;
      /* first time we found this variable! */
      if (!DB_MARKED(d0)) {
	
	/* store previous value */ 
	visited --;
	visited->addr = ptd0;
	CheckDBOverflow(1);
	/* variables need to be offset at read time */
	*ptd0 = (CELL)StoPoint;
#if SBA
	/* the copy we keep will be an empty variable   */
	*StoPoint++ = 0;
#else
#ifdef IDB_USE_MBIT
	/* say we've seen the variable, and make it point to its
	   offset */
	/* the copy we keep will be the current displacement   */
	*StoPoint = ((CELL)StoPoint | MBIT);
	StoPoint++;
#else
	/* the copy we keep will be the current displacement   */
	*StoPoint = (CELL)StoPoint;
	StoPoint++;
	*dbg->lr++ = ToSmall(displacement);
	db_check_trail(dbg->lr);
#endif
#endif
	/* indicate we found variables */
	vars_found++;
#ifdef COROUTINING
	if (SafeIsAttachedTerm((CELL)ptd0)) {
	  Term t[4];
	  int sz = to_visit-to_visit_base;

	  H = (CELL *)to_visit;
	  /* store the constraint away for: we need a back pointer to
	     the variable, the constraint in some cannonical form, what type
	     of constraint, and a list pointer */
	  t[0] = (CELL)ptd0;
	  t[1] = attas[ExtFromCell(ptd0)].to_term_op(ptd0);
	  t[2] = MkIntegerTerm(ExtFromCell(ptd0));
	  t[3] = ConstraintsTerm;
	  ConstraintsTerm = Yap_MkApplTerm(FunctorClist, 4, t);
	  if (H+sz >= ASP) {
	    goto error2;
	  }
	  memcpy((void *)H, (void *)(to_visit_base), sz*sizeof(CELL *));
	  to_visit_base = (CELL **)H;
	  to_visit = to_visit_base+sz;
	}
#endif
	continue;
      } else  {
	/* references need to be offset at read time */
#ifdef IDB_LINK_TABLE
	*dbg->lr++ = ToSmall(displacement);
	db_check_trail(dbg->lr);
#endif
	/* store the offset */
#ifdef IDB_USE_MBIT
	*StoPoint = d0 | MBIT;
#else
	*StoPoint = d0;
#endif
	StoPoint++;
	continue;
      }

    }

  }

  /* Do we still have compound terms to visit */
  if (to_visit > to_visit_base) {
#ifdef RATIONAL_TREES
    to_visit -= 4;
    pt0 = to_visit[0];
    pt0_end = to_visit[1];
    StoPoint = to_visit[2];
    pt0[-1] = (CELL)to_visit[3];
#else
    to_visit -= 3;
    pt0 = to_visit[0];
    pt0_end = to_visit[1];
    CheckDBOverflow(1);
    StoPoint = to_visit[2];
#endif
    goto loop;
  }

#ifdef COROUTINING
  /* we still may have constraints to do */
  if (ConstraintsTerm != TermNil &&
      !(RepAppl(ConstraintsTerm) >= tbase &&
	RepAppl(ConstraintsTerm) < StoPoint)
      ) {
    *attachmentsp = (CELL)(CodeMax+1);
    pt0 = RepAppl(ConstraintsTerm)+1;
    pt0_end = RepAppl(ConstraintsTerm)+4;
    StoPoint = CodeMax;
    *StoPoint++ = RepAppl(ConstraintsTerm)[0];
    ConstraintsTerm = AbsAppl(CodeMax);
    CheckDBOverflow(1);
    CodeMax += 5;
    goto loop;
  }
#endif
  /* we're done */
  *vars_foundp = vars_found;
  DB_UNWIND_CUNIF();
#ifdef COROUTINING
  H = origH;
#endif
  return(CodeMax);

 error:
  Yap_Error_TYPE = OUT_OF_HEAP_ERROR;
  Yap_Error_Size = 1024+((char *)AuxSp-(char *)CodeMaxBase);
  *vars_foundp = vars_found;
#ifdef RATIONAL_TREES
  while (to_visit > to_visit_base) {
    to_visit -= 4;
    pt0 = to_visit[0];
    pt0_end = to_visit[1];
    StoPoint = to_visit[2];
    pt0[-1] = (CELL)to_visit[3];
  }
#endif
  DB_UNWIND_CUNIF();
#ifdef COROUTINING
  H = origH;
#endif
  return(NULL);

 error2:
  Yap_Error_TYPE = OUT_OF_STACK_ERROR;
  *vars_foundp = vars_found;
#ifdef RATIONAL_TREES
  while (to_visit > to_visit_base) {
    to_visit -= 4;
    pt0 = to_visit[0];
    pt0_end = to_visit[1];
    StoPoint = to_visit[2];
    pt0[-1] = (CELL)to_visit[3];
  }
#endif
  DB_UNWIND_CUNIF();
#ifdef COROUTINING
  H = origH;
#endif
  return(NULL);

#if !OS_HANDLES_TR_OVERFLOW
 error_tr_overflow:
  Yap_Error_TYPE = OUT_OF_TRAIL_ERROR;
  *vars_foundp = vars_found;
#ifdef RATIONAL_TREES
  while (to_visit > to_visit_base) {
    to_visit -= 4;
    pt0 = to_visit[0];
    pt0_end = to_visit[1];
    StoPoint = to_visit[2];
    pt0[-1] = (CELL)to_visit[3];
  }
#endif
  DB_UNWIND_CUNIF();
#ifdef COROUTINING
  H = origH;
#endif
  return(NULL);
#endif
#if THREADS
#undef Yap_REGS
#define Yap_REGS (*Yap_regp)  
#endif /* THREADS */
}


#ifdef SFUNC
/*
 * The sparse terms existing in the structure are to be included now. This
 * means simple copy for constant terms but, some care about variables If
 * they have appeared before, we will know by their position number 
 */
static void 
sf_include(SFKeep *sfp, struct db_globs *dbg)
	SFKeep         *sfp;
{
  Term            Tm = sfp->SName;
  CELL           *tp = ArgsOfSFTerm(Tm);
  Register Term  *StoPoint = ntp;
  CELL           *displacement = CodeAbs;
  CELL            arg_no;
  Term            tvalue;
  int             j = 3;

  if (sfp->SFather != NIL)
    *(sfp->SFather) = AbsAppl(displacement);
  *StoPoint++ = FunctorOfTerm(Tm);
  *dbg->lr++ = ToSmall(displacement + 1);
  db_check_trail(dbg->lr);
  *StoPoint++ = (Term) (displacement + 1);
  while (*tp) {
    arg_no = *tp++;
    tvalue = Derefa(tp++);
    if (IsVarTerm(tvalue)) {
      if (((VarKeep *) tvalue)->NOfVars != 0) {
	*StoPoint++ = arg_no;
	*dbg->lr++ = ToSmall(displacement + j);
	db_check_trail(dbg->lr);
	if (((VarKeep *) tvalue)->New == 0)
	  *StoPoint++ = ((VarKeep *) tvalue)->New = Unsigned(displacement + j);
	else
	  *StoPoint++ = ((VarKeep *) tvalue)->New;
	j += 2;
      }
    } else if (IsAtomicTerm(tvalue)) {
      *StoPoint++ = arg_no;
      *StoPoint++ = tvalue;
      j += 2;
    } else {
      Yap_Error_TYPE = TYPE_ERROR_DBTERM;
      Yap_Error_Term = d0;
      Yap_ErrorMessage = "wrong term in SF";
      return(NULL);
    }
  }
  *StoPoint++ = 0;
  ntp = StoPoint;
  CodeAbs = displacement + j;
}
#endif

/*
 * This function is used to check if one of the terms in the idb is the
 * constant to_compare 
 */
inline static DBRef 
check_if_cons(DBRef p, Term to_compare)
{
  while (p != NIL
	 && (p->Flags & (DBCode | ErasedMask | DBVar | DBNoVars | DBComplex)
	     || p->DBT.Entry != Unsigned(to_compare)))
    p = NextDBRef(p);
  return (p);
}

/*
 * This function is used to check if one of the terms in the idb is a prolog
 * variable 
 */
static DBRef 
check_if_var(DBRef p)
{
  while (p != NIL &&
	 p->Flags & (DBCode | ErasedMask | DBAtomic | DBNoVars | DBComplex ))
    p = NextDBRef(p);
  return (p);
}

/*
 * This function is used to check if a Prolog complex term with variables
 * already exists in the idb for that key. The comparison is alike ==, but
 * only the relative binding of variables, not their position is used. The
 * comparison is done using the function cmpclls only. The function could
 * only fail if a functor was matched to a Prolog term, but then, it should
 * have failed before because the structure of term would have been very
 * different 
 */
static DBRef 
check_if_wvars(DBRef p, unsigned int NOfCells, CELL *BTptr)
{
  CELL           *memptr;

  do {
    while (p != NIL &&
	   p->Flags & (DBCode | ErasedMask | DBAtomic | DBNoVars | DBVar))
      p = NextDBRef(p);
    if (p == NIL)
      return (p);
    memptr = CellPtr(&(p->DBT.Contents));
    if (NOfCells == p->DBT.NOfCells
	&& cmpclls(memptr, BTptr, NOfCells))
      return (p);
    else
      p = NextDBRef(p);
  } while (TRUE);
  return (NIL);
}

#ifdef IDB_LINK_TABLE

static int 
scheckcells(int NOfCells, register CELL *m1, register CELL *m2, link_entry *lp, register CELL bp)
{
  CELL            base = Unsigned(m1);
  link_entry         *lp1;

  while (NOfCells-- > 0) {
    Register CELL   r1, r2;

    r1 = *m1++;
    r2 = *m2++;
    if (r1 == r2)
      continue;
    else if (r2 + bp == r1) {
      /* link pointers may not have been generated in the */
      /* same order */
      /* make sure r1 is really an offset. */
      lp1 = lp;
      r1 = m1 - (CELL *)base;
      while (*lp1 != r1 && *lp1)
	lp1++;
      if (!(*lp1))
	return (FALSE);
      /* keep the old link pointer for future search. */
      /* vsc: this looks like a bug!!!! */
      /* *lp1 = *lp++; */
    } else {
      return (FALSE);
    }
  }
  return (TRUE);
}
#endif

/*
 * the cousin of the previous, but with things a bit more sophisticated.
 * mtchcells, if an error was an found, needs to test ........ 
 */
static DBRef 
check_if_nvars(DBRef p, unsigned int NOfCells, CELL *BTptr, struct db_globs *dbg)
{
  CELL           *memptr;

  do {
    while (p != NIL &&
	   p->Flags & (DBCode | ErasedMask | DBAtomic | DBComplex | DBVar))
      p = NextDBRef(p);
    if (p == NIL)
      return (p);
    memptr = CellPtr(p->DBT.Contents);
#ifdef IDB_LINK_TABLE
    if (scheckcells(NOfCells, memptr, BTptr, dbg->LinkAr, Unsigned(p->DBT.Contents-1)))
#else
      if (NOfCells == *memptr++
	  && cmpclls(memptr, BTptr, NOfCells))
#endif
	return (p);
      else
	p = NextDBRef(p);
  } while (TRUE);
  return (NIL);
}

static DBRef
generate_dberror_msg(int errnumb, UInt sz, char *msg)
{
  Yap_Error_Size = sz;
  Yap_Error_TYPE = errnumb;
  Yap_Error_Term = TermNil;
  Yap_ErrorMessage = msg;
  return NULL;
}

static DBRef
CreateDBWithDBRef(Term Tm, DBProp p)
{
  DBRef pp, dbr = DBRefOfTerm(Tm);
  DBTerm *ppt;

  if (p == NULL) {
    ppt = (DBTerm *)AllocDBSpace(sizeof(DBTerm)+2*sizeof(CELL));
    if (ppt == NULL) {
      return generate_dberror_msg(OUT_OF_HEAP_ERROR, TermNil, "could not allocate space");
    }
    pp = (DBRef)ppt;
  } else {
    pp = AllocDBSpace(DBLength(2*sizeof(DBRef)));
    if (pp == NULL) {
      return generate_dberror_msg(OUT_OF_HEAP_ERROR, 0, "could not allocate space");
    }
    pp->id = FunctorDBRef;
    pp->Flags = DBNoVars|DBComplex|DBWithRefs;
    INIT_LOCK(pp->lock);
    INIT_DBREF_COUNT(pp);
    ppt = &(pp->DBT);
  }
  if (dbr->Flags & LogUpdMask) {
    LogUpdClause *cl = (LogUpdClause *)dbr;
    cl->ClRefCount++;
  } else {
    dbr->NOfRefsTo++;
  }
  ppt->Entry = Tm;
  ppt->NOfCells = 0;
  ppt->Contents[0] = (CELL)NULL;
  ppt->Contents[1] = (CELL)dbr;
  ppt->DBRefs = (DBRef *)(ppt->Contents+2);
#ifdef COROUTINING
  ppt->attachments = 0L;
#endif
  return pp;
}

static DBTerm *
CreateDBTermForAtom(Term Tm, UInt extra_size) {
  DBTerm *ppt;
  ADDR ptr;

  ptr = (ADDR)AllocDBSpace(extra_size+sizeof(DBTerm));
  if (ptr == NULL) {
    return (DBTerm *)generate_dberror_msg(OUT_OF_HEAP_ERROR, 0, "could not allocate space");
  }
  ppt = (DBTerm *)(ptr+extra_size);
  ppt->NOfCells = 0;
  ppt->DBRefs = NULL;
#ifdef COROUTINING
  ppt->attachments = 0;
#endif
  ppt->DBRefs = NULL;
  ppt->Entry = Tm;
  return ppt;
}

static DBTerm *
CreateDBTermForVar(UInt extra_size)
{
  DBTerm *ppt;
  ADDR ptr;

  ptr = (ADDR)AllocDBSpace(extra_size+sizeof(DBTerm));
  if (ptr == NULL) {
    return (DBTerm *)generate_dberror_msg(OUT_OF_HEAP_ERROR, 0, "could not allocate space");
  }
  ppt = (DBTerm *)(ptr+extra_size);
  ppt->NOfCells = 0;
  ppt->DBRefs = NULL;
#ifdef COROUTINING
  ppt->attachments = 0;
#endif
  ppt->DBRefs = NULL;
  ppt->Entry = (CELL)(&(ppt->Entry));
  return ppt;
}

static DBRef
CreateDBRefForAtom(Term Tm, DBProp p, int InFlag, struct db_globs *dbg) {
  Register DBRef  pp;
  SMALLUNSGN      flag;

  flag = DBAtomic;
  if (InFlag & MkIfNot && (dbg->found_one = check_if_cons(p->First, Tm)))
    return dbg->found_one;
  pp = AllocDBSpace(DBLength(NIL));
  if (pp == NIL) {
    return generate_dberror_msg(OUT_OF_HEAP_ERROR, 0, "could not allocate space");
  }
  pp->id = FunctorDBRef;
  INIT_LOCK(pp->lock);
  INIT_DBREF_COUNT(pp);
  pp->Flags = flag;
  pp->Code = NULL;
  pp->DBT.Entry = Tm;
  pp->DBT.DBRefs = NULL;
  pp->DBT.NOfCells = 0;
#ifdef COROUTINING
  pp->DBT.attachments = 0;
#endif
  return(pp);
}

static DBRef
CreateDBRefForVar(Term Tm, DBProp p, int InFlag, struct db_globs *dbg) {
  Register DBRef  pp;

  if (InFlag & MkIfNot && (dbg->found_one = check_if_var(p->First)))
    return dbg->found_one;
  pp = AllocDBSpace(DBLength(NULL));
  if (pp == NULL) {
    return generate_dberror_msg(OUT_OF_HEAP_ERROR, 0, "could not allocate space");
  }
  pp->id = FunctorDBRef;
  pp->Flags = DBVar;
  pp->DBT.Entry = (CELL) Tm;
  pp->Code = NULL;
  pp->DBT.NOfCells = 0;
  pp->DBT.DBRefs = NULL;
#ifdef COROUTINING
  pp->DBT.attachments = 0;
#endif
  INIT_LOCK(pp->lock);
  INIT_DBREF_COUNT(pp);
  return(pp);
}

static DBRef 
CreateDBStruct(Term Tm, DBProp p, int InFlag, int *pstat, UInt extra_size, struct db_globs *dbg)
{
  Register Term   tt, *nar = NIL;
  SMALLUNSGN      flag;
#ifdef IDB_LINK_TABLE
  int NOfLinks = 0;
#endif
  /* place DBRefs in ConsultStack */
  DBRef    *TmpRefBase = (DBRef *)Yap_TrailTop;
  CELL	   *CodeAbs;	/* how much code did we find	 */
  int vars_found;

  Yap_Error_TYPE = YAP_NO_ERROR;

  if (p == NULL) {
    if (IsVarTerm(Tm)) {
#ifdef COROUTINING
      if (!SafeIsAttachedTerm(Tm)) {
#endif
	DBRef out = (DBRef)CreateDBTermForVar(extra_size);
	*pstat = TRUE;
	return out;
#ifdef COROUTINING
      }
#endif
    } else if (IsAtomOrIntTerm(Tm)) {
      DBRef out = (DBRef)CreateDBTermForAtom(Tm, extra_size);
      *pstat = FALSE;
      return out;
    }
  } else {
    if (IsVarTerm(Tm)
#ifdef COROUTINING
      && !SafeIsAttachedTerm(Tm)
#endif
      ) {
      *pstat = TRUE;
      return CreateDBRefForVar(Tm, p, InFlag, dbg);
    } else if (IsAtomOrIntTerm(Tm)) {
      return CreateDBRefForAtom(Tm, p, InFlag, dbg);
    }
  }
  {
    DBTerm *ppt, *ppt0;
    DBRef  pp, pp0;
    Term           *ntp0, *ntp;
    unsigned int    NOfCells = 0;
#ifdef COROUTINING
    CELL attachments = 0;
#endif

    dbg->tofref = TmpRefBase;
    /* compound term */
    
    if (p == NULL) {
      ADDR ptr = Yap_PreAllocCodeSpace();
      ppt0 = (DBTerm *)(ptr+extra_size);
      pp0 = (DBRef)ppt0;
    } else {
      pp0 = (DBRef)Yap_PreAllocCodeSpace();
      ppt0 = &(pp0->DBT);
    }
    ntp0 = ppt0->Contents;
#ifdef IDB_LINK_TABLE
    dbg->lr = dbg->LinkAr = (link_entry *)TR;
#endif
#ifdef COROUTINING
    /* attachment */ 
    if (IsVarTerm(Tm)) {
      tt = (CELL)(ppt0->Contents);
      ntp = MkDBTerm(VarOfTerm(Tm), VarOfTerm(Tm), ntp0, ntp0+1, ntp0-1,
		     &attachments,
		     &vars_found,
		     dbg);
      if (ntp == NULL) {
	Yap_ReleasePreAllocCodeSpace((ADDR)pp0);
	return(NULL);
      }
    } else
#endif
    if (IsPairTerm(Tm)) {
      /* avoid null pointers!! */
      tt = AbsPair(ppt0->Contents);
      ntp = MkDBTerm(RepPair(Tm), RepPair(Tm)+1, ntp0, ntp0+2, ntp0-1,
#ifdef COROUTINING
		     &attachments,
#endif
		     &vars_found, dbg);
      if (ntp == NULL) {
	Yap_ReleasePreAllocCodeSpace((ADDR)pp0);
	return(NULL);
      }
    }
    else {
      unsigned int arity;
      Functor fun;

      tt = AbsAppl(ppt0->Contents);
      /* we need to store the functor manually */
      fun = FunctorOfTerm(Tm);
      if (IsExtensionFunctor(fun)) {
	switch((CELL)fun) {
	case (CELL)FunctorDouble:
	  ntp = copy_double(ntp0, RepAppl(Tm));
	  break;
	case (CELL)FunctorDBRef:
	  Yap_ReleasePreAllocCodeSpace((ADDR)pp0);
	  return CreateDBWithDBRef(Tm, p);
#ifdef USE_GMP
	case (CELL)FunctorBigInt:
	  ntp = copy_big_int(ntp0, RepAppl(Tm));
	  break;
#endif
	default: /* LongInt */
	  ntp = copy_long_int(ntp0, RepAppl(Tm));
	  break;
	}
      } else {
	*ntp0 = (CELL)fun;
	arity = ArityOfFunctor(fun);
	ntp = MkDBTerm(RepAppl(Tm)+1,
		       RepAppl(Tm)+arity,
		       ntp0+1, ntp0+1+arity, ntp0-1,
#ifdef COROUTINING
		       &attachments,
#endif
		       &vars_found, dbg);
	if (ntp == NULL) {
	  Yap_ReleasePreAllocCodeSpace((ADDR)pp0);
	  return(NULL);
	}
      }
    } 
    CodeAbs = (CELL *)((CELL)ntp-(CELL)ntp0);
    if (Yap_Error_TYPE) {
      Yap_ReleasePreAllocCodeSpace((ADDR)pp0);
      return (NULL);	/* Error Situation */
    }
    NOfCells = ntp - ntp0;	/* End Of Code Info */
#ifdef IDB_LINK_TABLE
    *dbg->lr++ = 0;
    NOfLinks = (dbg->lr - dbg->LinkAr);
#endif
    if (vars_found || InFlag & InQueue) {

      /*
       * Take into account the fact that one needs an entry
       * for the number of links 
       */
      flag = DBComplex;
#ifdef IDB_LINK_TABLE
      CodeAbs++;	/* We have one more cell */
      CodeAbs += CellPtr(dbg->lr) - CellPtr(dbg->LinkAr);
      if ((CELL *)((char *)ntp0+(CELL)CodeAbs) > AuxSp) {
	Yap_Error_Size = (UInt)DBLength(CodeAbs);
	Yap_Error_TYPE = OUT_OF_HEAP_ERROR;
	Yap_ReleasePreAllocCodeSpace((ADDR)pp0);
	return(NULL);
      }
#endif
      if ((InFlag & MkIfNot) && (dbg->found_one = check_if_wvars(p->First, NOfCells, ntp0))) {
	Yap_ReleasePreAllocCodeSpace((ADDR)pp0);
	return dbg->found_one;
      }
    } else {
      flag = DBNoVars;
      if ((InFlag & MkIfNot) && (dbg->found_one = check_if_nvars(p->First, NOfCells, ntp0, dbg))) {
	Yap_ReleasePreAllocCodeSpace((ADDR)pp0);
	return dbg->found_one;
      }
    }
    if (dbg->tofref != TmpRefBase) {
      CodeAbs += (TmpRefBase - dbg->tofref) + 1;
      if ((CELL *)((char *)ntp0+(CELL)CodeAbs) > AuxSp) {
	Yap_Error_Size = (UInt)DBLength(CodeAbs);
	Yap_Error_TYPE = OUT_OF_HEAP_ERROR;
	Yap_ReleasePreAllocCodeSpace((ADDR)pp0);
	return(NULL);
      }
      flag |= DBWithRefs;
    }
#ifdef IDB_LINK_TABLE
#if SIZEOF_LINK_ENTRY==2
    if (Unsigned(CodeAbs) >= 0x40000) {
      Yap_ReleasePreAllocCodeSpace((ADDR)pp0);
      return generate_dberror_msg(SYSTEM_ERROR, 0, "trying to store term larger than 256KB");
    }
#endif
#endif
    if (p == NULL) {
      ADDR ptr = Yap_AllocCodeSpace((CELL)CodeAbs+extra_size+sizeof(DBTerm));
      ppt = (DBTerm *)(ptr+extra_size);
      if (ptr == NULL) {
	Yap_ReleasePreAllocCodeSpace((ADDR)pp0);
	return generate_dberror_msg(OUT_OF_HEAP_ERROR, (UInt)DBLength(CodeAbs), "heap crashed against stacks");
      }
      pp = (DBRef)ppt;
    } else {
      pp = AllocDBSpace(DBLength(CodeAbs));
      if (pp == NULL) {
	Yap_ReleasePreAllocCodeSpace((ADDR)pp0);
	return generate_dberror_msg(OUT_OF_HEAP_ERROR, (UInt)DBLength(CodeAbs), "heap crashed against stacks");
      }
      pp->id = FunctorDBRef;
      pp->Flags = flag;
      INIT_LOCK(pp->lock);
      INIT_DBREF_COUNT(pp);
      ppt = &(pp->DBT);
    }
    if (flag & DBComplex) {
#ifdef IDB_LINK_TABLE
      link_entry         *woar;
#endif /* IDB_LINK_TABLE */

      ppt->NOfCells = NOfCells;
#ifdef COROUTINING
      ppt->attachments = attachments;
#endif
      if (pp0 != pp) {
	nar = ppt->Contents;
#ifdef IDB_LINK_TABLE
	nar = (Term *) cpcells(CellPtr(nar), ntp0, Unsigned(NOfCells));
#endif
#ifdef IDB_USE_MBIT
	memcpy((void *)nar, (const void *)ntp0,
	       (size_t)((NOfCells+1)*sizeof(CELL)));
	nar += NOfCells+1;
#endif
      } else {
	nar = ppt->Contents + Unsigned(NOfCells);
      }
#ifdef IDB_LINK_TABLE
      woar = (link_entry *)nar;
      memcpy((void *)woar,(const void *)dbg->LinkAr,(size_t)(NOfLinks*sizeof(link_entry)));
      woar += NOfLinks;
#ifdef ALIGN_LONGS
#if SIZEOF_INT_P==8
      while ((Unsigned(woar) & 7) != 0)
	woar++;		
#else
      if ((Unsigned(woar) & 3) != 0)
	woar++;
#endif
#endif
      nar = (Term *) (woar);
#endif
      *pstat = TRUE;
    } else if (flag & DBNoVars) {
      if (pp0 != pp) {
	nar = (Term *) cpcells(CellPtr(ppt->Contents), ntp0, Unsigned(NOfCells));
      } else {
#ifdef IDB_LINK_TABLE
	nar = ppt->Contents + Unsigned(NOfCells)+1;
#endif
#ifdef IDB_USE_MBIT
	/* we still need to link */
	nar = (Term *) linkcells(ntp0, NOfCells);
#endif
      }
      ppt->NOfCells = NOfCells;
    }
    if (ppt != ppt0) {
#ifdef IDB_LINK_TABLE
      linkblk(dbg->LinkAr, CellPtr(ppt->Contents-1), (CELL)ppt-(CELL)ppt0);
#endif
      ppt->Entry = AdjustIDBPtr(tt,(CELL)ppt-(CELL)ppt0);
#ifdef COROUTINING
      if (ppt->attachments)
	ppt->attachments = AdjustIDBPtr(ppt->attachments,(CELL)ppt-(CELL)ppt0);
#endif
    } else {
      ppt->Entry = tt;
    }
    if (flag & DBWithRefs) {
      DBRef *ptr = TmpRefBase, *rfnar = (DBRef *)nar;

      *rfnar++ = NULL;
      while (ptr != dbg->tofref)
	*rfnar++ = *--ptr;
      ppt->DBRefs = rfnar;
    } else {
      ppt->DBRefs = NULL;
    }      
    Yap_ReleasePreAllocCodeSpace((ADDR)pp0);
    return pp;
  }
}

static DBRef 
record(int Flag, Term key, Term t_data, Term t_code)
{
  Register Term   twork = key;
  Register DBProp p;
  Register DBRef  x;
  int needs_vars;
  struct db_globs dbg;

  s_dbg = &dbg;
  dbg.found_one = NULL;
#ifdef SFUNC
  FathersPlace = NIL;
#endif
  if (EndOfPAEntr(p = FetchDBPropFromKey(twork, Flag & MkCode, TRUE, "record/3"))) {
    return(NULL);
  }
  if ((x = CreateDBStruct(t_data, p, Flag, &needs_vars, 0, &dbg)) == NULL) {
    return (NULL);
  }
  if ((Flag & MkIfNot) && dbg.found_one)
    return (NULL);
  TRAIL_REF(x);
  if (x->Flags & (DBNoVars|DBComplex))
    x->Mask = EvalMasks(t_data, &x->Key);
  else
    x->Mask = x->Key = 0;
  if (Flag & MkCode)
    x->Flags |= DBCode;
  else
    x->Flags |= DBNoCode;
  x->Parent = p;
#if defined(YAPOR) || defined(THREADS)
  x->Flags |= DBClMask;
  x->ref_count = 1;
#else
  x->Flags |= (InUseMask | DBClMask);
#endif
  x->NOfRefsTo = 0;
  WRITE_LOCK(p->DBRWLock);
  if (p->F0 == NULL) {
    p->F0 = p->L0 = x;
    x->p = x->n = NULL;
  } else {
    if (Flag & MkFirst) {
      x->n = p->F0;
      p->F0->p = x;
      p->F0 = x;
      x->p = NULL;
    } else {
      x->p = p->L0;
      p->L0->n = x;
      p->L0 = x;
      x->n = NULL;
    }
  }
  if (p->First == NIL) {
    p->First = p->Last = x;
    x->Prev = x->Next = NIL;
  } else if (Flag & MkFirst) {
    x->Prev = NIL;
    (p->First)->Prev = x;
    x->Next = p->First;
    p->First = x;
  } else {
    x->Next = NIL;
    (p->Last)->Next = x;
    x->Prev = p->Last;
    p->Last = x;
  }
  if (Flag & MkCode) {
    x->Code = (yamop *) IntegerOfTerm(t_code);
  }
  WRITE_UNLOCK(p->DBRWLock);
  return (x);
}

/* add a new entry next to an old one */
static DBRef 
record_at(int Flag, DBRef r0, Term t_data, Term t_code)
{
  Register DBProp p;
  Register DBRef  x;
  int needs_vars;
  struct db_globs dbg;

  s_dbg = &dbg;
#ifdef SFUNC
  FathersPlace = NIL;
#endif
  p = r0->Parent;
  if ((x = CreateDBStruct(t_data, p, Flag, &needs_vars, 0, &dbg)) == NULL) {
    return (NULL);
  }
  TRAIL_REF(x);
  if (x->Flags & (DBNoVars|DBComplex))
    x->Mask = EvalMasks(t_data, &x->Key);
  else
    x->Mask = x->Key = 0;
  if (Flag & MkCode)
    x->Flags |= DBCode;
  else
    x->Flags |= DBNoCode;
  x->Parent = p;
#if defined(YAPOR) || defined(THREADS)
  x->Flags |= DBClMask;
  x->ref_count = 1;
#else
  x->Flags |= (InUseMask | DBClMask);
#endif
  x->NOfRefsTo = 0;
  WRITE_LOCK(p->DBRWLock);
  if (Flag & MkFirst) {
    x->n = r0;
    x->p = r0->p;
    if (p->F0 == r0) {
      p->F0 = x;
    } else {
      r0->p->n = x;
    }
    r0->p = x;
  } else {
    x->p = r0;
    x->n = r0->n;
    if (p->L0 == r0) {
      p->L0 = x;
    } else {
      r0->n->p = x;
    }
    r0->n = x;
  }
  if (Flag & MkFirst) {
    x->Prev = r0->Prev;
    x->Next = r0;
    if (p->First == r0) {
      p->First = x;
    } else {
      r0->Prev->Next = x;
    }
    r0->Prev = x;
  } else {
    x->Next = r0->Next;
    x->Prev = r0;
    if (p->Last == r0) {
      p->Last = x;
    } else {
      r0->Next->Prev = x;
    }
    r0->Next = x;
  }
  if (Flag & WithRef) {
    x->Code = (yamop *) IntegerOfTerm(t_code);
  }
  WRITE_UNLOCK(p->DBRWLock);
  return (x);
}


static LogUpdClause *
record_lu(PredEntry *pe, Term t, int position)
{
  yamop *ipc;
  DBTerm *x;
  LogUpdClause *cl;
  int needs_vars = FALSE;
  struct db_globs dbg;

  s_dbg = &dbg;
  ipc = NEXTOP(((LogUpdClause *)NULL)->ClCode,e);
  if ((x = (DBTerm *)CreateDBStruct(t, NULL, 0, &needs_vars, (UInt)ipc, &dbg)) == NULL) {
    return NULL; /* crash */
  }
  cl = (LogUpdClause *)((ADDR)x-(UInt)ipc);
  ipc = cl->ClCode;
  cl->Id = FunctorDBRef;
  cl->ClFlags = LogUpdMask;
  cl->ClSource = x;
  cl->ClRefCount = 0;
  cl->ClPred = pe;
  cl->ClExt = NULL;
  cl->ClPrev = cl->ClNext = NULL;
  cl->ClSize = ((CODEADDR)&(x->Contents)-(CODEADDR)cl)+x->NOfCells*sizeof(CELL);
#if defined(YAPOR) || defined(THREADS)
  INIT_LOCK(cl->ClLock);
  INIT_CLREF_COUNT(cl);
#endif
  if (needs_vars)
    ipc->opc = Yap_opcode(_copy_idb_term);
  else
    ipc->opc = Yap_opcode(_unify_idb_term);
  WRITE_LOCK(pe->PRWLock);
#if defined(YAPOR) || defined(THREADS)
  WPP = pe;
#endif
  Yap_add_logupd_clause(pe, cl, (position == MkFirst ? 2 : 0));
#if defined(YAPOR) || defined(THREADS)
  WPP = NULL;
#endif
  WRITE_UNLOCK(pe->PRWLock);
  return cl;
}


/* recorda(+Functor,+Term,-Ref) */
static Int 
p_rcda(void)
{
  /* Idiotic xlc's cpp does not work with ARG1 within MkDBRefTerm */
  Term            TRef, t1 = Deref(ARG1), t2 = Deref(ARG2);
  PredEntry *pe = NULL;

  if (!IsVarTerm(Deref(ARG3)))
    return (FALSE);
  pe = find_lu_entry(t1);
 restart_record:
  Yap_Error_Size = 0;
  if (pe) {
    LogUpdClause *cl;
    cl = record_lu(pe, t2, MkFirst);
    if (cl != NULL) {
      TRAIL_CLREF(cl);
#if defined(YAPOR) || defined(THREADS)
      INC_CLREF_COUNT(cl);
#else
      cl->ClFlags |= InUseMask;
#endif
      TRef = MkDBRefTerm((DBRef)cl);
    } else {
      TRef = TermNil;
    }
  } else { 
    TRef = MkDBRefTerm(record(MkFirst, t1, t2, Unsigned(0)));
  }
  switch(Yap_Error_TYPE) {
  case YAP_NO_ERROR:
    return (Yap_unify(ARG3, TRef));
  case OUT_OF_STACK_ERROR:
    if (!Yap_gc(3, ENV, P)) {
      Yap_Error(OUT_OF_STACK_ERROR, TermNil, Yap_ErrorMessage);
      return(FALSE);
    }
    goto recover_record;
  case OUT_OF_TRAIL_ERROR:
    if (!Yap_growtrail(64 * 1024L)) {
      Yap_Error(OUT_OF_TRAIL_ERROR, TermNil, "YAP could not grow trail in recorda/3");
      return(FALSE);
    }
    goto recover_record;
  case OUT_OF_HEAP_ERROR:
    if (!Yap_ExpandPreAllocCodeSpace(Yap_Error_Size)) {
      return FALSE;
    }
    goto recover_record;
  default:
    Yap_Error(Yap_Error_TYPE, Yap_Error_Term, Yap_ErrorMessage);
    return(FALSE);
  }
 recover_record:
  Yap_Error_TYPE = YAP_NO_ERROR;
  t1 = Deref(ARG1);
  t2 = Deref(ARG2);
  goto restart_record;
}

/* '$recordap'(+Functor,+Term,-Ref) */
static Int 
p_rcdap(void)
{
  Term            TRef, t1 = Deref(ARG1), t2 = Deref(ARG2);

  if (!IsVarTerm(Deref(ARG3)))
    return FALSE;
 restart_record:
  Yap_Error_Size = 0;
  TRef = MkDBRefTerm(record(MkFirst | MkCode, t1, t2, Unsigned(0)));
  switch(Yap_Error_TYPE) {
  case YAP_NO_ERROR:
    return Yap_unify(ARG3, TRef);
  case OUT_OF_STACK_ERROR:
    if (!Yap_gc(3, ENV, P)) {
      Yap_Error(OUT_OF_STACK_ERROR, TermNil, Yap_ErrorMessage);
      return FALSE;
    }
    goto recover_record;
  case OUT_OF_TRAIL_ERROR:
    if(!Yap_growtrail (sizeof(CELL) * 16 * 1024L)) {
      Yap_Error(OUT_OF_TRAIL_ERROR, TermNil, "YAP could not grow trail in recorda/3");
      return FALSE;
    }
    goto recover_record;
  case OUT_OF_HEAP_ERROR:
    if (!Yap_ExpandPreAllocCodeSpace(Yap_Error_Size)) {
      return FALSE;
    }
    goto recover_record;
  default:
    Yap_Error(Yap_Error_TYPE, Yap_Error_Term, Yap_ErrorMessage);
    return FALSE;
  }
 recover_record:
  Yap_Error_TYPE = YAP_NO_ERROR;
  t1 = Deref(ARG1);
  t2 = Deref(ARG2);
  goto restart_record;
}

/* recorda_at(+Functor,+Term,-Ref) */
static Int 
p_rcda_at(void)
{
  /* Idiotic xlc's cpp does not work with ARG1 within MkDBRefTerm */
  Term            TRef, t1 = Deref(ARG1), t2 = Deref(ARG2);

  if (!IsVarTerm(Deref(ARG3)))
    return (FALSE);
  if (IsVarTerm(t1)) {
      Yap_Error(INSTANTIATION_ERROR, t1, "recorda_at/3");
      return(FALSE);
  }
  if (!IsDBRefTerm(t1)) {
      Yap_Error(TYPE_ERROR_DBREF, t1, "recorda_at/3");
      return(FALSE);
  }
 restart_record:
  Yap_Error_Size = 0;
  TRef = MkDBRefTerm(record_at(MkFirst, DBRefOfTerm(t1), t2, Unsigned(0)));
  switch(Yap_Error_TYPE) {
  case YAP_NO_ERROR:
    return (Yap_unify(ARG3, TRef));
  case OUT_OF_STACK_ERROR:
    if (!Yap_gc(3, ENV, P)) {
      Yap_Error(OUT_OF_STACK_ERROR, TermNil, Yap_ErrorMessage);
      return(FALSE);
    }
    goto recover_record;
  case OUT_OF_TRAIL_ERROR:
    if(!Yap_growtrail (sizeof(CELL) * 16 * 1024L)) {
      Yap_Error(OUT_OF_TRAIL_ERROR, TermNil, "YAP could not grow trail in recorda/3");
      return FALSE;
    }
    goto recover_record;
  case OUT_OF_HEAP_ERROR:
    if (!Yap_ExpandPreAllocCodeSpace(Yap_Error_Size)) {
      return FALSE;
    }
    goto recover_record;
  default:
    Yap_Error(Yap_Error_TYPE, Yap_Error_Term, Yap_ErrorMessage);
    return(FALSE);
  }
 recover_record:
  Yap_Error_TYPE = YAP_NO_ERROR;
  t1 = Deref(ARG1);
  t2 = Deref(ARG2);
  goto restart_record;
}

/* recordz(+Functor,+Term,-Ref) */
static Int 
p_rcdz(void)
{
  Term            TRef, t1 = Deref(ARG1), t2 = Deref(ARG2);
  PredEntry *pe;

  if (!IsVarTerm(Deref(ARG3)))
    return (FALSE);
  pe = find_lu_entry(t1);
 restart_record:
  Yap_Error_Size = 0;
  if (pe) {
    LogUpdClause *cl = record_lu(pe, t2, MkLast);
    if (cl != NULL) {
      TRAIL_CLREF(cl);
#if defined(YAPOR) || defined(THREADS)
      INC_CLREF_COUNT(cl);
#else
      cl->ClFlags |= InUseMask;
#endif
      TRef = MkDBRefTerm((DBRef)cl);
    } else {
      TRef = TermNil;
    }
  } else { 
    TRef = MkDBRefTerm(record(MkLast, t1, t2, Unsigned(0)));
  }
  switch(Yap_Error_TYPE) {
  case YAP_NO_ERROR:
    return (Yap_unify(ARG3, TRef));
  case OUT_OF_STACK_ERROR:
    if (!Yap_gc(3, ENV, P)) {
      Yap_Error(OUT_OF_STACK_ERROR, TermNil, Yap_ErrorMessage);
      return FALSE;
    }
    goto recover_record;
  case OUT_OF_TRAIL_ERROR:
    if(!Yap_growtrail (sizeof(CELL) * 16 * 1024L)) {
      Yap_Error(OUT_OF_TRAIL_ERROR, TermNil, "YAP could not grow trail in recorda/3");
      return FALSE;
    }
    goto recover_record;
  case OUT_OF_HEAP_ERROR:
    if (!Yap_ExpandPreAllocCodeSpace(Yap_Error_Size)) {
      return FALSE;
    }
    goto recover_record;
  default:
    Yap_Error(Yap_Error_TYPE, Yap_Error_Term, Yap_ErrorMessage);
    return(FALSE);
  }
 recover_record:
  Yap_Error_TYPE = YAP_NO_ERROR;
  t1 = Deref(ARG1);
  t2 = Deref(ARG2);
  goto restart_record;
}

/* '$recordzp'(+Functor,+Term,-Ref) */
static Int 
p_rcdzp(void)
{
  Term            TRef, t1 = Deref(ARG1), t2 = Deref(ARG2);

  if (!IsVarTerm(Deref(ARG3)))
    return (FALSE);
 restart_record:
  Yap_Error_Size = 0;
  TRef = MkDBRefTerm(record(MkLast | MkCode, t1, t2, Unsigned(0)));
  switch(Yap_Error_TYPE) {
  case YAP_NO_ERROR:
    return (Yap_unify(ARG3, TRef));
  case OUT_OF_STACK_ERROR:
    if (!Yap_gc(3, ENV, P)) {
      Yap_Error(OUT_OF_STACK_ERROR, TermNil, Yap_ErrorMessage);
      return(FALSE);
    }
    goto recover_record;
  case OUT_OF_TRAIL_ERROR:
    if(!Yap_growtrail (sizeof(CELL) * 16 * 1024L)) {
      Yap_Error(OUT_OF_TRAIL_ERROR, TermNil, "YAP could not grow trail in recorda/3");
      return FALSE;
    }
    goto recover_record;
  case OUT_OF_HEAP_ERROR:
    if (!Yap_ExpandPreAllocCodeSpace(Yap_Error_Size)) {
      return FALSE;
    }
    goto recover_record;
  default:
    Yap_Error(Yap_Error_TYPE, Yap_Error_Term, Yap_ErrorMessage);
    return(FALSE);
  }
 recover_record:
  Yap_Error_TYPE = YAP_NO_ERROR;
  t1 = Deref(ARG1);
  t2 = Deref(ARG2);
  goto restart_record;
}

/* recordz_at(+Functor,+Term,-Ref) */
static Int 
p_rcdz_at(void)
{
  /* Idiotic xlc's cpp does not work with ARG1 within MkDBRefTerm */
  Term            TRef, t1 = Deref(ARG1), t2 = Deref(ARG2);

  if (!IsVarTerm(Deref(ARG3)))
    return (FALSE);
  if (IsVarTerm(t1)) {
      Yap_Error(INSTANTIATION_ERROR, t1, "recordz_at/3");
      return(FALSE);
  }
  if (!IsDBRefTerm(t1)) {
      Yap_Error(TYPE_ERROR_DBREF, t1, "recordz_at/3");
      return(FALSE);
  }
 restart_record:
  Yap_Error_Size = 0;
  TRef = MkDBRefTerm(record_at(MkLast, DBRefOfTerm(t1), t2, Unsigned(0)));
  switch(Yap_Error_TYPE) {
  case YAP_NO_ERROR:
    return (Yap_unify(ARG3, TRef));
  case OUT_OF_STACK_ERROR:
    if (!Yap_gc(3, ENV, P)) {
      Yap_Error(OUT_OF_STACK_ERROR, TermNil, Yap_ErrorMessage);
      return(FALSE);
    }
    goto recover_record;
  case OUT_OF_TRAIL_ERROR:
    if(!Yap_growtrail (sizeof(CELL) * 16 * 1024L)) {
      Yap_Error(OUT_OF_TRAIL_ERROR, TermNil, "YAP could not grow trail in recorda/3");
      return FALSE;
    }
    goto recover_record;
  case OUT_OF_HEAP_ERROR:
    if (!Yap_ExpandPreAllocCodeSpace(Yap_Error_Size)) {
      return FALSE;
    }
    goto recover_record;
  default:
    Yap_Error(Yap_Error_TYPE, Yap_Error_Term, Yap_ErrorMessage);
    return(FALSE);
  }
 recover_record:
  Yap_Error_TYPE = YAP_NO_ERROR;
  t1 = Deref(ARG1);
  t2 = Deref(ARG2);
  goto restart_record;
}

/* '$record_stat_source'(+Functor,+Term) */
static Int 
p_rcdstatp(void)
{
  Term t1 = Deref(ARG1), t2 = Deref(ARG2), t3 = Deref(ARG3);
  int mk_first;
  Term TRef;

  if (IsVarTerm(t3) || !IsIntTerm(t3))
    return (FALSE);
  if (IsVarTerm(t3) || !IsIntTerm(t3))
    return (FALSE);
  mk_first = ((IntOfTerm(t3) % 4) == 2);
 restart_record:
  Yap_Error_Size = 0;
  if (mk_first)
    TRef = MkDBRefTerm(record(MkFirst | MkCode, t1, t2, MkIntTerm(0)));
  else
    TRef = MkDBRefTerm(record(MkLast | MkCode, t1, t2, MkIntTerm(0)));
  switch(Yap_Error_TYPE) {
  case YAP_NO_ERROR:
    return (Yap_unify(ARG4,TRef));
  case OUT_OF_STACK_ERROR:
    if (!Yap_gc(3, ENV, P)) {
      Yap_Error(OUT_OF_STACK_ERROR, TermNil, Yap_ErrorMessage);
      return(FALSE);
    }
    goto recover_record;
  case OUT_OF_TRAIL_ERROR:
    if(!Yap_growtrail (sizeof(CELL) * 16 * 1024L)) {
      Yap_Error(OUT_OF_TRAIL_ERROR, TermNil, "YAP could not grow trail in recorda/3");
      return FALSE;
    }
    goto recover_record;
  case OUT_OF_HEAP_ERROR:
    if (!Yap_ExpandPreAllocCodeSpace(Yap_Error_Size)) {
      return FALSE;
    }
    goto recover_record;
  default:
    Yap_Error(Yap_Error_TYPE, Yap_Error_Term, Yap_ErrorMessage);
    return(FALSE);
  }
 recover_record:
  Yap_Error_TYPE = YAP_NO_ERROR;
  t1 = Deref(ARG1);
  t2 = Deref(ARG2);
  goto restart_record;
}

/* '$recordap'(+Functor,+Term,-Ref,+CRef) */
static Int 
p_drcdap(void)
{
  Term            TRef, t1 = Deref(ARG1), t2 = Deref(ARG2), t4 = Deref(ARG4);

  if (!IsVarTerm(Deref(ARG3)))
    return (FALSE);
  if (IsVarTerm(t4) || !IsIntegerTerm(t4))
    return (FALSE);
 restart_record:
  Yap_Error_Size = 0;
  TRef = MkDBRefTerm(record(MkFirst | MkCode | WithRef,
			    t1, t2, t4));
  switch(Yap_Error_TYPE) {
  case YAP_NO_ERROR:
    return (Yap_unify(ARG3, TRef));
  case OUT_OF_STACK_ERROR:
    if (!Yap_gc(4, ENV, P)) {
      Yap_Error(OUT_OF_STACK_ERROR, TermNil, Yap_ErrorMessage);
      return(FALSE);
    }
    goto recover_record;
  case OUT_OF_TRAIL_ERROR:
    if(!Yap_growtrail (sizeof(CELL) * 16 * 1024L)) {
      Yap_Error(OUT_OF_TRAIL_ERROR, TermNil, "YAP could not grow trail in recorda/3");
      return FALSE;
    }
    goto recover_record;
  case OUT_OF_HEAP_ERROR:
    if (!Yap_ExpandPreAllocCodeSpace(Yap_Error_Size)) {
      return FALSE;
    }
    goto recover_record;
  default:
    Yap_Error(Yap_Error_TYPE, Yap_Error_Term, Yap_ErrorMessage);
    return(FALSE);
  }
 recover_record:
  Yap_Error_TYPE = YAP_NO_ERROR;
  t1 = Deref(ARG1);
  t2 = Deref(ARG2);
  t4 = Deref(ARG4);
  goto restart_record;
}

/* '$recordzp'(+Functor,+Term,-Ref,+CRef) */
static Int 
p_drcdzp(void)
{
  Term            TRef, t1 = Deref(ARG1), t2 = Deref(ARG2), t4 =  Deref(ARG4);

  if (!IsVarTerm(Deref(ARG3)))
    return (FALSE);
  if (IsVarTerm(t4) || !IsIntegerTerm(t4))
    return (FALSE);
 restart_record:
  Yap_Error_Size = 0;
  TRef = MkDBRefTerm(record(MkLast | MkCode | WithRef,
			    t1, t2, t4));
  switch(Yap_Error_TYPE) {
  case YAP_NO_ERROR:
    return Yap_unify(ARG3, TRef);
  case OUT_OF_STACK_ERROR:
    if (!Yap_gc(4, ENV, P)) {
      Yap_Error(OUT_OF_STACK_ERROR, TermNil, Yap_ErrorMessage);
      return(FALSE);
    }
    goto recover_record;
  case OUT_OF_TRAIL_ERROR:
    if(!Yap_growtrail (sizeof(CELL) * 16 * 1024L)) {
      Yap_Error(OUT_OF_TRAIL_ERROR, TermNil, "YAP could not grow trail in recorda/3");
      return FALSE;
    }
    goto recover_record;
  case OUT_OF_HEAP_ERROR:
    if (!Yap_ExpandPreAllocCodeSpace(Yap_Error_Size)) {
      return FALSE;
    }
    goto recover_record;
  default:
    Yap_Error(Yap_Error_TYPE, Yap_Error_Term, Yap_ErrorMessage);
    return(FALSE);
  }
 recover_record:
  Yap_Error_TYPE = YAP_NO_ERROR;
  t1 = Deref(ARG1);
  t2 = Deref(ARG2);
  t4 = Deref(ARG4);
  goto restart_record;
}

static Int
p_still_variant(void)
{
  CELL *old_h = B->cp_h;
  tr_fr_ptr   old_tr = B->cp_tr;
  Term t1 = Deref(ARG1), t2 = Deref(ARG2);
  DBTerm *dbt;
  DBRef dbr;
  
  if (IsVarTerm(t1) || !IsDBRefTerm(t1)) {
    return (FALSE);
    /* limited sanity checking */
    if (dbr->id != FunctorDBRef) {
      return FALSE;
    }
  } else {
    dbr = DBRefOfTerm(t1);
  }
  /* ok, we assume there was a choicepoint before we copied the term */

  /* skip binding for argument variable */
  old_tr++;
  if (dbr->Flags & LogUpdMask) {
    LogUpdClause *cl = (LogUpdClause *)dbr;

    if (old_tr == TR-1) {
      if (TrailTerm(old_tr) != CLREF_TO_TRENTRY(cl))
	return FALSE;
    } else if (old_tr != TR)
      return FALSE;
    if (Yap_op_from_opcode(cl->ClCode->opc) == _unify_idb_term) {
      return TRUE;
    } else {
      dbt = cl->ClSource;
    }
  } else {
    if (old_tr == TR-1) {
      if (TrailTerm(old_tr) != REF_TO_TRENTRY(dbr))
	return FALSE;
    } else if (old_tr != TR)
      return FALSE;
    if (dbr->Flags & (DBNoVars|DBAtomic))
      return TRUE;
    if (dbr->Flags & DBVar)
      return IsVarTerm(t2);
    dbt = &(dbr->DBT);
  }
#ifdef IDB_LINK_TABLE
  /*
    we checked the trail, so we are sure only variables in the new term
    were bound
  */
  {
    link_entry *lp = (link_entry *)(dbt->Contents+dbt->NOfCells);
    link_entry link;

    while ((link = *lp++)) {
      Term t2 = Deref(old_h[link-1]);
      if (IsUnboundVar((CELL)(dbt->Contents+(link-1)))) {
	if (IsVarTerm(t2)) {
	  Yap_unify(t2,MkAtomTerm(AtomFoundVar));
	} else {
	  return FALSE;
	}
      }
    }
  }
#else /* IDB_LINK_TABLE */
  not IMPLEMENTED;
#endif
  return TRUE;
}


#ifdef COROUTINING
static void
copy_attachments(CELL *ts)
{
  while (TRUE) {
    Term t;
    /* store away in case there is an overflow */
    *--ASP = ts[3];
    attas[IntegerOfTerm(ts[2])].term_to_op(ts[1], ts[0]);
    t = *ASP;
    ASP++;
    if (t == TermNil) return;
    ts = RepAppl(t)+1;
  }
}
#endif

static Term
GetDBLUKey(PredEntry *ap)
{
  READ_LOCK(ap->PRWLock);
  if (ap->PredFlags & NumberDBPredFlag) {
    Int id = ap->src.IndxId;
    READ_UNLOCK(ap->PRWLock);
    return MkIntegerTerm(id);
  } else if (ap->PredFlags & AtomDBPredFlag) {
    Atom at = (Atom)ap->FunctorOfPred;
    READ_UNLOCK(ap->PRWLock);
    return MkAtomTerm(at);
  } else {
    Functor f = ap->FunctorOfPred;
    READ_UNLOCK(ap->PRWLock);
    return Yap_MkNewApplTerm(f,ArityOfFunctor(f));
  }
}

static int 
UnifyDBKey(DBRef DBSP, PropFlags flags, Term t)
{
  DBProp p = DBSP->Parent;
  Term t1, tf;

  READ_LOCK(p->DBRWLock);
  /* get the key */
  if (p->ArityOfDB == 0) {
    t1 = MkAtomTerm((Atom)(p->FunctorOfDB));
  } else {
    t1 = Yap_MkNewApplTerm(p->FunctorOfDB,p->ArityOfDB);
  }
  if ((p->KindOfPE & CodeDBBit) && (flags & CodeDBBit)) {
    Term t[2];
    if (p->ModuleOfDB)
      t[0] = p->ModuleOfDB;
    else
      t[0] = TermProlog;
    t[1] = t1;
    tf = Yap_MkApplTerm(FunctorModule, 2, t);
  } else if (!(flags & CodeDBBit)) {
    tf = t1;
  } else {
    return FALSE;
  }
  READ_UNLOCK(p->DBRWLock);
  return(Yap_unify(tf,t));
}


static int 
UnifyDBNumber(DBRef DBSP, Term t)
{
  DBProp p = DBSP->Parent;
  DBRef ref;
  Int i = 1;

  READ_LOCK(p->DBRWLock);
  ref = p->First;
  while (ref != NIL) {
    if (ref == DBSP) break;
    if (!DEAD_REF(ref)) i++;
    ref = ref->Next;
  }
  if (ref == NIL)
    return FALSE;
  READ_UNLOCK(p->DBRWLock);
  return(Yap_unify(MkIntegerTerm(i),t));
}


static Term 
GetDBTerm(DBTerm *DBSP)
{
  Term t = DBSP->Entry;

  if (IsVarTerm(t) 
#if COROUTINING
      && !DBSP->attachments
#endif
      ) {
    return MkVarTerm();
  } else if (IsAtomOrIntTerm(t)) {
    return t;
  } else {
    CELL           *HOld = H;
    CELL           *HeapPtr;
    CELL           *pt;
    CELL            NOf;

    if (!(NOf = DBSP->NOfCells)) {
      return t;
    }
    pt = CellPtr(DBSP->Contents);
    if (H+NOf > ASP-CalculateStackGap()) {
      if (Yap_PrologMode & InErrorMode) {
	if (H+NOf > ASP)
	  fprintf(Yap_stderr, "\n\n [ FATAL ERROR: No Stack for Error Handling ]\n");
	  Yap_exit( 1);
      } else {
	Yap_Error_Size = NOf*sizeof(CELL);
	return((Term)0);
      }
    }
    HeapPtr = cpcells(HOld, pt, NOf);
    pt += HeapPtr - HOld;
    H = HeapPtr;
#ifdef IDB_LINK_TABLE
    {
      link_entry *lp = (link_entry *)pt;
      linkblk(lp, HOld-1, (CELL)HOld-(CELL)(DBSP->Contents));
    }
#endif
#ifdef COROUTINING
    if (DBSP->attachments != 0L)  {
      *--ASP = (CELL)HOld;
      copy_attachments((CELL *)AdjustIDBPtr(DBSP->attachments,(CELL)HOld-(CELL)(DBSP->Contents)));
      HOld = CellPtr(*ASP++);
    }
#endif
    return AdjustIDBPtr(t,Unsigned(HOld)-(CELL)(DBSP->Contents));
  }
}

static Term 
GetDBTermFromDBEntry(DBRef DBSP)
{
  if (DBSP->Flags & (DBNoVars | DBAtomic))
    return DBSP->DBT.Entry;
  return GetDBTerm(&(DBSP->DBT));
}

static void
init_int_keys(void) {
  INT_KEYS = (Prop *)Yap_AllocCodeSpace(sizeof(Prop)*INT_KEYS_SIZE);
  if (INT_KEYS != NULL) {
    UInt i = 0;
    Prop *p = INT_KEYS;
    for (i = 0; i < INT_KEYS_SIZE; i++) {
      p[0] = NIL;
      p++;
    }
  }
}

static void
init_int_lu_keys(void) {
  INT_LU_KEYS = (Prop *)Yap_AllocCodeSpace(sizeof(Prop)*INT_KEYS_SIZE);
  if (INT_LU_KEYS != NULL) {
    UInt i = 0;
    Prop *p = INT_LU_KEYS;
    for (i = 0; i < INT_KEYS_SIZE; i++) {
      p[0] = NULL;
      p++;
    }
  }
}

static int
resize_int_keys(UInt new_size) {
  Prop *new;
  UInt i;

  YAPEnterCriticalSection();
  if (INT_KEYS == NULL) {
    INT_KEYS_SIZE = new_size;
    YAPLeaveCriticalSection();
    return(TRUE);
  }
  new = (Prop *)Yap_AllocCodeSpace(sizeof(Prop)*new_size);
  if (new == NULL) {
    YAPLeaveCriticalSection();
    Yap_Error_TYPE = OUT_OF_HEAP_ERROR;
    Yap_Error_Term = TermNil;
    Yap_ErrorMessage = "could not allocate space";
    return(FALSE);
  }
  for (i = 0; i < new_size; i++) {
    new[i] = NIL;
  }
  for (i = 0; i < INT_KEYS_SIZE; i++) {
    if (INT_KEYS[i] != NIL) {
      Prop p0 = INT_KEYS[i];
      while (p0 != NIL) {
	DBProp p = RepDBProp(p0);
	CELL key = (CELL)(p->FunctorOfDB);
	UInt hash_key = (CELL)key % new_size;
	p0 = p->NextOfPE;
	p->NextOfPE = new[hash_key];
	new[hash_key] = AbsDBProp(p);
      }
    }
  }
  Yap_FreeCodeSpace((char *)INT_KEYS);
  INT_KEYS = new;
  INT_KEYS_SIZE = new_size;
  INT_KEYS_TIMESTAMP++;
  if (INT_KEYS_TIMESTAMP == MAX_ABS_INT)
    INT_KEYS_TIMESTAMP = 0;
  YAPLeaveCriticalSection();
  return(TRUE);
}

static PredEntry *
find_lu_int_key(Int key)
{
  UInt hash_key = (CELL)key % INT_KEYS_SIZE;
  Prop p0;
  
  if (INT_LU_KEYS != NULL) {
    p0 = INT_LU_KEYS[hash_key];
    while (p0) {
      PredEntry *pe = RepPredProp(p0);
      if (pe->src.IndxId == key) {
	return pe;
      }
      p0 = pe->NextOfPE;
    }
  }
  if (UPDATE_MODE == UPDATE_MODE_LOGICAL &&
      find_int_key(key) == NULL) {
    return new_lu_int_key(key);
  }
  return NULL;
}

static DBProp
find_int_key(Int key)
{
  UInt hash_key = (CELL)key % INT_KEYS_SIZE;
  Prop p0;
  
  if (INT_KEYS == NULL) {
    return NULL;
  }
  p0 = INT_KEYS[hash_key];
  while (p0) {
    DBProp p = RepDBProp(p0);
    if (p->FunctorOfDB == (Functor)key) return(p);
    p0 = p->NextOfPE;
  }
  return NULL;
}

static PredEntry *
new_lu_int_key(Int key)
{
  UInt hash_key = (CELL)key % INT_KEYS_SIZE;
  PredEntry *p;
  Prop p0;
  Functor fe;
  
  if (INT_LU_KEYS == NULL) {
    init_int_lu_keys();
    if (INT_LU_KEYS == NULL) {
      Yap_Error_TYPE = OUT_OF_HEAP_ERROR;
      Yap_Error_Term = TermNil;
      Yap_ErrorMessage = "could not allocate space";
      return NULL;
    }
  }
  fe = Yap_MkFunctor(Yap_FullLookupAtom("$integer"),3);
  WRITE_LOCK(fe->FRWLock);
  p0 = Yap_NewPredPropByFunctor(fe,IDB_MODULE);
  p = RepPredProp(p0);
  p->NextOfPE = INT_LU_KEYS[hash_key];
  p->src.IndxId = key;
  p->PredFlags |= LogUpdatePredFlag|NumberDBPredFlag;
  p->ArityOfPE = 3;
  p->OpcodeOfPred = Yap_opcode(_op_fail);
  p->cs.p_code.TrueCodeOfPred = p->CodeOfPred = FAILCODE;
  INT_LU_KEYS[hash_key] = p0;
  return p;
}

static PredEntry *
new_lu_entry(Term t)
{
  Prop p0;
  PredEntry *pe;

  if (IsApplTerm(t)) {
    Functor f = FunctorOfTerm(t);

    WRITE_LOCK(f->FRWLock);
    p0 = Yap_NewPredPropByFunctor(f,IDB_MODULE);
  } else if (IsAtomTerm(t)) {
    Atom at = AtomOfTerm(t);

    WRITE_LOCK(RepAtom(at)->ARWLock);
    p0 = Yap_NewPredPropByAtom(at,IDB_MODULE);
  } else {
    WRITE_LOCK(FunctorList->FRWLock);
    p0 = Yap_NewPredPropByFunctor(FunctorList,IDB_MODULE);
  }
  pe = RepPredProp(p0);
  pe->PredFlags |= LogUpdatePredFlag;
  if (IsAtomTerm(t)) {
    pe->PredFlags |= AtomDBPredFlag;
  }
  pe->ArityOfPE = 3;
  pe->OpcodeOfPred = Yap_opcode(_op_fail);
  pe->cs.p_code.TrueCodeOfPred = pe->CodeOfPred = FAILCODE;
  return pe;
}

static DBProp
find_entry(Term t)
{
  Atom at;
  UInt arity;

  if (IsVarTerm(t)) {
    return(RepDBProp(NIL));
  } else if (IsAtomTerm(t)) {
    at = AtomOfTerm(t);
    arity = 0;

  } else if (IsIntegerTerm(t)) {
    return find_int_key(IntegerOfTerm(t));
  } else if (IsApplTerm(t)) {
    Functor f = FunctorOfTerm(t);

    at = NameOfFunctor(f);
    arity = ArityOfFunctor(f);
  } else {
    at = AtomDot;
    arity = 2;
  }
  return RepDBProp(FindDBProp(RepAtom(at), 0, arity, 0));
}

static PredEntry *
find_lu_entry(Term t)
{
  Prop p;

  if (IsVarTerm(t)) {
    Yap_Error(INSTANTIATION_ERROR, t, "while accessing database key");
    return NULL;
  }
  if (IsIntegerTerm(t)) {
    return find_lu_int_key(IntegerOfTerm(t));
  } else if (IsApplTerm(t)) {
    Functor f = FunctorOfTerm(t);

    if (IsExtensionFunctor(f)) {
      Yap_Error(TYPE_ERROR_KEY, t, "while accessing database key");
      return NULL;
    }
    p = Yap_GetPredPropByFuncInThisModule(FunctorOfTerm(t),IDB_MODULE);
  } else if (IsAtomTerm(t)) {
    p = Yap_GetPredPropByAtomInThisModule(AtomOfTerm(t),IDB_MODULE);
  } else {
    p = Yap_GetPredPropByFuncInThisModule(FunctorList,IDB_MODULE);
  }
  if (p == NIL) {
    if (UPDATE_MODE == UPDATE_MODE_LOGICAL && !find_entry(t)) {
      return new_lu_entry(t);
    } else {
      return NULL;
    }
  }
  return RepPredProp(p);
}


static DBProp
FetchIntDBPropFromKey(Int key, int flag, int new, char *error_mssg)
{
  Functor fun = (Functor)key;
  UInt hash_key = (CELL)key % INT_KEYS_SIZE;
  Prop p0;

  if (INT_KEYS == NULL) {
    init_int_keys();
    if (INT_KEYS == NULL) {
      Yap_Error_TYPE = OUT_OF_HEAP_ERROR;
      Yap_Error_Term = TermNil;
      Yap_ErrorMessage = "could not allocate space";
      return(NULL);
    }
  }
  p0 = INT_KEYS[hash_key];
  while (p0 != NIL) {
    DBProp p = RepDBProp(p0);
    if (p->FunctorOfDB == fun) return(p);
    p0 = p->NextOfPE;
  }
  /* p is NULL, meaning we did not find the functor */
  if (new) {
    DBProp p;
    /* create a new DBProp				 */
    p = (DBProp) Yap_AllocAtomSpace(sizeof(*p));
    p->KindOfPE = DBProperty|flag;
    p->F0 = p->L0 = NULL;
    p->ArityOfDB = 0;
    p->First = p->Last = NULL;
    p->ModuleOfDB = 0;
    p->FunctorOfDB = fun;
    p->NextOfPE = INT_KEYS[hash_key];
    INIT_RWLOCK(p->DBRWLock);
    INT_KEYS[hash_key] = AbsDBProp(p);
    return(p);
  } else {
    return(RepDBProp(NULL));
  }
}

static DBProp
FetchDBPropFromKey(Term twork, int flag, int new, char *error_mssg)
{
  Atom At;
  Int arity;
  Term dbmod;

  if (flag & MkCode) {
    if (IsVarTerm(twork)) {
      Yap_Error(INSTANTIATION_ERROR, twork, error_mssg);
      return RepDBProp(NULL);
    }
    if (!IsApplTerm(twork)) {
      Yap_Error(SYSTEM_ERROR, twork, "missing module");
      return RepDBProp(NULL);
    } else {
      Functor f = FunctorOfTerm(twork);
      if (f != FunctorModule) {
	Yap_Error(SYSTEM_ERROR, twork, "missing module");
	return RepDBProp(NULL);
      }
      dbmod = ArgOfTerm(1, twork);
      if (IsVarTerm(dbmod)) {
	Yap_Error(INSTANTIATION_ERROR, twork, "var in module");
	return(RepDBProp(NIL));
      }
      if (!IsAtomTerm(dbmod)) {
	Yap_Error(TYPE_ERROR_ATOM, twork, "not atom in module");
	return(RepDBProp(NIL));
      }
      twork = ArgOfTerm(2, twork);
    }
  } else {
    dbmod = 0;
    
  }
  if (IsVarTerm(twork)) {
    Yap_Error(INSTANTIATION_ERROR, twork, error_mssg);
    return(RepDBProp(NIL));
  } else if (IsAtomTerm(twork)) {
    arity = 0, At = AtomOfTerm(twork);
  } else if (IsIntegerTerm(twork)) {
    return(FetchIntDBPropFromKey(IntegerOfTerm(twork), flag, new, error_mssg));
  } else if (IsApplTerm(twork)) {
    Register Functor f = FunctorOfTerm(twork);
    if (IsExtensionFunctor(f)) {
      Yap_Error(TYPE_ERROR_KEY, twork, error_mssg);
      return(RepDBProp(NIL));
    }
    At = NameOfFunctor(f);
    arity = ArityOfFunctor(f);
  } else if (IsPairTerm(twork)) {
    At = AtomDot;
    arity = 2;
  } else {
    Yap_Error(TYPE_ERROR_KEY, twork,error_mssg);
    return(RepDBProp(NIL));
  }
  if (new) {
    DBProp p;
    AtomEntry *ae = RepAtom(At);

    WRITE_LOCK(ae->ARWLock);
    if (EndOfPAEntr(p = RepDBProp(FindDBPropHavingLock(ae, flag, arity, dbmod)))) {
     /* create a new DBProp				 */
      int OLD_UPDATE_MODE = UPDATE_MODE;
      if (flag & MkCode) {
	PredEntry *pp;
	pp = RepPredProp(Yap_GetPredPropHavingLock(At, arity, dbmod));

	if (!EndOfPAEntr(pp)) {
	  READ_LOCK(pp->PRWLock);
	  if(pp->PredFlags & LogUpdatePredFlag)
	    UPDATE_MODE = UPDATE_MODE_LOGICAL;
	  READ_UNLOCK(pp->PRWLock);
	}

      }
      p = (DBProp) Yap_AllocAtomSpace(sizeof(*p));
      p->KindOfPE = DBProperty|flag;
      p->F0 = p->L0 = NULL;
      UPDATE_MODE = OLD_UPDATE_MODE;
      p->ArityOfDB = arity;
      p->First = p->Last = NIL;
      p->ModuleOfDB = dbmod;
      /* This is NOT standard but is QUITE convenient */
      INIT_RWLOCK(p->DBRWLock);
      if (arity == 0)
	p->FunctorOfDB = (Functor) At;
      else
	p->FunctorOfDB = Yap_UnlockedMkFunctor(ae,arity);
      p->NextOfPE = ae->PropsOfAE;
      ae->PropsOfAE = AbsDBProp(p);
    }
    WRITE_UNLOCK(ae->ARWLock);
    return(p);
  } else
    return(RepDBProp(FindDBProp(RepAtom(At), flag, arity, dbmod)));
}


static DBRef 
nth_recorded_log(LogUpdDBProp AtProp, Int Count)
{
  Yap_Error(SYSTEM_ERROR, TermNil, Yap_ErrorMessage);
  return NULL;
}


/* Finds a term recorded under the key ARG1			 */
static Int 
nth_recorded(DBProp AtProp, Int Count)
{
  Register DBRef  ref;

  READ_LOCK(AtProp->DBRWLock);
  if (AtProp->KindOfPE & 0x1) {
    ref = nth_recorded_log((LogUpdDBProp)AtProp, Count);
    if (ref == NULL) {
      READ_UNLOCK(AtProp->DBRWLock);
      return FALSE;
    }
  } else {
    ref = AtProp->First;
    Count--;
    while (ref != NULL
	   && DEAD_REF(ref))
      ref = NextDBRef(ref);
    if (ref == NULL) {
      READ_UNLOCK(AtProp->DBRWLock);
      return FALSE;
    }
    while (Count) {
      Count--;
      ref = NextDBRef(ref);
      while (ref != NULL
	     && DEAD_REF(ref))
	ref = NextDBRef(ref);
      if (ref == NULL) {
	READ_UNLOCK(AtProp->DBRWLock);
	return FALSE;
      }
    }
  }
#if defined(YAPOR) || defined(THREADS)
  LOCK(ref->lock);
  READ_UNLOCK(AtProp->DBRWLock);
  TRAIL_REF(ref);		/* So that fail will erase it */
  INC_DBREF_COUNT(ref);
  UNLOCK(ref->lock);
#else
  if (!(ref->Flags & InUseMask)) {
    ref->Flags |= InUseMask;
    TRAIL_REF(ref);	/* So that fail will erase it */
  }
  READ_UNLOCK(AtProp->DBRWLock);
#endif
  return Yap_unify(MkDBRefTerm(ref),ARG3);
}

static Int
p_nth_instance(void)
{
  DBProp          AtProp;
  Term            TCount;
  Int             Count;
  Term t3 = Deref(ARG3);

  if (!IsVarTerm(t3)) {
    if (!IsDBRefTerm(t3)) {
      Yap_Error(TYPE_ERROR_DBREF,t3,"nth_instance/3");
      return FALSE;
    } else {
      DBRef ref = DBRefOfTerm(t3);
      LOCK(ref->lock);
      if (ref == NULL
	  || DEAD_REF(ref)
	  || !UnifyDBKey(ref,0,ARG1)
	  || !UnifyDBNumber(ref,ARG2)) {
	UNLOCK(ref->lock);
	return(FALSE);
      } else {
	UNLOCK(ref->lock);
	return(TRUE);
      }
    }
  }
  if (EndOfPAEntr(AtProp = FetchDBPropFromKey(Deref(ARG1), 0, FALSE, "nth_instance/3"))) {
    return(FALSE);
  }
  TCount = Deref(ARG2);
  if (IsVarTerm(TCount)) {
    Yap_Error(INSTANTIATION_ERROR, TCount, "nth_instance/3");
    return (FALSE);
  }
  if (!IsIntegerTerm(TCount)) {
    Yap_Error(TYPE_ERROR_INTEGER, TCount, "nth_instance/3");
    return (FALSE);
  }
  Count = IntegerOfTerm(TCount);
  if (Count <= 0) {
    if (Count) 
      Yap_Error(DOMAIN_ERROR_NOT_LESS_THAN_ZERO, TCount, "nth_instance/3");
    else
      Yap_Error(DOMAIN_ERROR_NOT_ZERO, TCount, "nth_instance/3");
    return (FALSE);
  }
  return nth_recorded(AtProp,Count);
}

static Int
p_nth_instancep(void)
{
  DBProp          AtProp;
  Term            TCount;
  Int             Count;
  Term            t3 = Deref(ARG3);

  if (!IsVarTerm(t3)) {
    if (!IsDBRefTerm(t3)) {
      Yap_Error(TYPE_ERROR_DBREF,t3,"nth_instance/3");
      return FALSE;
    } else {
      DBRef ref = DBRefOfTerm(t3);
      LOCK(ref->lock);
      if (ref == NULL
	  || DEAD_REF(ref)
	  || !UnifyDBKey(ref,CodeDBBit,ARG1)
	  || !UnifyDBNumber(ref,ARG2)) {
	UNLOCK(ref->lock);
	return(FALSE);
      } else {
	UNLOCK(ref->lock);
	return(TRUE);
      }
    }
  }
  if (EndOfPAEntr(AtProp = FetchDBPropFromKey(Deref(ARG1), MkCode, FALSE, "nth_instance/3"))) {
    return(FALSE);
  }
  TCount = Deref(ARG2);
  if (IsVarTerm(TCount)) {
    Yap_Error(INSTANTIATION_ERROR, TCount, "recorded_at/4");
    return (FALSE);
  }
  if (!IsIntegerTerm(TCount)) {
    Yap_Error(TYPE_ERROR_INTEGER, TCount, "recorded_at/4");
    return (FALSE);
  }
  Count = IntegerOfTerm(TCount);
  if (Count <= 0) {
    if (Count) 
      Yap_Error(DOMAIN_ERROR_NOT_LESS_THAN_ZERO, TCount, "recorded_at/4");
    else
      Yap_Error(DOMAIN_ERROR_NOT_ZERO, TCount, "recorded_at/4");
    return (FALSE);
  }
  return nth_recorded(AtProp,Count);
}

static Int
p_db_key(void)
{
  Register Term   twork = Deref(ARG1);	/* fetch the key */
  DBProp          AtProp;

  if (EndOfPAEntr(AtProp = FetchDBPropFromKey(twork, 0, TRUE, "db_key/3"))) {
    /* should never happen */
    return(FALSE);
  }
  return(Yap_unify(ARG2,MkIntegerTerm((Int)AtProp)));
}

/* Finds a term recorded under the key ARG1			 */
static Int 
i_recorded(DBProp AtProp, Term t3)
{
  Term            TermDB, TRef;
  Register DBRef  ref;
  Term twork;

  READ_LOCK(AtProp->DBRWLock);
  ref = AtProp->First;
  while (ref != NULL
	 && DEAD_REF(ref))
    ref = NextDBRef(ref);
  READ_UNLOCK(AtProp->DBRWLock);
  if (ref == NULL) {
    cut_fail();
  }
  twork = Deref(ARG2);	/* now working with ARG2 */
  if (IsVarTerm(twork)) {
    EXTRA_CBACK_ARG(3,2) = MkIntegerTerm(0);
    EXTRA_CBACK_ARG(3,3) = MkIntegerTerm(0);
    B->cp_h = H;
    while ((TermDB = GetDBTermFromDBEntry(ref)) == (CELL)0) {
      /* make sure the garbage collector sees what we want it to see! */
      EXTRA_CBACK_ARG(3,1) = (CELL)ref;
      /* oops, we are in trouble, not enough stack space */
      if (!Yap_gcl(Yap_Error_Size, 3, ENV, CP)) {
	Yap_Error(OUT_OF_STACK_ERROR, TermNil, Yap_ErrorMessage);
	return(FALSE);
      }
      twork = Deref(ARG2);
      t3 = Deref(ARG3);
    }
    if (!Yap_unify(twork, TermDB)) {
      cut_fail();
    }
  } else if (IsAtomOrIntTerm(twork)) {
    EXTRA_CBACK_ARG(3,2) = MkIntegerTerm(0);
    EXTRA_CBACK_ARG(3,3) = MkIntegerTerm((Int)twork);
    B->cp_h = H;
    READ_LOCK(AtProp->DBRWLock);
    do {
      if (((twork == ref->DBT.Entry) || IsVarTerm(ref->DBT.Entry)) &&
	  !DEAD_REF(ref))
	break;
      ref = NextDBRef(ref);
      if (ref == NIL) {
	READ_UNLOCK(AtProp->DBRWLock);
	cut_fail();
      }
    } while (TRUE);
    READ_UNLOCK(AtProp->DBRWLock);
  } else {
    CELL key;
    CELL mask = EvalMasks(twork, &key);

    B->cp_h = H;
    READ_LOCK(AtProp->DBRWLock);
    do {
      while ((mask & ref->Key) != (key & ref->Mask) && !DEAD_REF(ref)) {
	ref = NextDBRef(ref);
	if (ref == NULL) {
	  READ_UNLOCK(AtProp->DBRWLock);
	  cut_fail();
	}
      }
      if ((TermDB = GetDBTermFromDBEntry(ref)) != (CELL)0) {
	if (Yap_unify(TermDB, ARG2)) {
	  /* success */
	  EXTRA_CBACK_ARG(3,2) = MkIntegerTerm(((Int)mask));
	  EXTRA_CBACK_ARG(3,3) = MkIntegerTerm(((Int)key));
	  B->cp_h = H;
	  break;
	} else {
	  while ((ref = NextDBRef(ref)) != NULL
		 && DEAD_REF(ref));
	  if (ref == NULL) {
	    READ_UNLOCK(AtProp->DBRWLock);
	    cut_fail();
	  }
	}
      } else {
	/* make sure the garbage collector sees what we want it to see! */
	EXTRA_CBACK_ARG(3,1) = (CELL)ref;
	READ_UNLOCK(AtProp->DBRWLock);
	EXTRA_CBACK_ARG(3,2) = MkIntegerTerm(((Int)mask));
	EXTRA_CBACK_ARG(3,3) = MkIntegerTerm(((Int)key));
	/* oops, we are in trouble, not enough stack space */
	if (!Yap_gcl(Yap_Error_Size, 3, ENV, CP)) {
	  Yap_Error(OUT_OF_STACK_ERROR, TermNil, Yap_ErrorMessage);
	  return(FALSE);
	}
	READ_LOCK(AtProp->DBRWLock);
      }
    } while (TRUE);
    READ_UNLOCK(AtProp->DBRWLock);
  }
  EXTRA_CBACK_ARG(3,1) = (CELL)ref;
  /* This should be after any non-tagged terms, because the routines in grow.c
     go from upper to lower addresses */
  TRef = MkDBRefTerm(ref);
#if defined(YAPOR) || defined(THREADS)
  LOCK(ref->lock);
  TRAIL_REF(ref);		/* So that fail will erase it */
  INC_DBREF_COUNT(ref);
  UNLOCK(ref->lock);
#else
  if (!(ref->Flags & InUseMask)) {
    ref->Flags |= InUseMask;
    TRAIL_REF(ref);		/* So that fail will erase it */
  }
#endif
  return (Yap_unify(ARG3, TRef));
}

static Int 
c_recorded(int flags)
{
  Term            TermDB, TRef;
  Register DBRef  ref, ref0;
  CELL           *PreviousHeap = H;
  CELL            mask, key;
  Term t1;

  t1 = EXTRA_CBACK_ARG(3,1);
  ref0 = (DBRef)t1;
  READ_LOCK(ref0->Parent->DBRWLock);
  ref = NextDBRef(ref0);
  if (ref == NIL) {
    if (ref0->Flags & ErasedMask) {
      ref = ref0;
      while ((ref = ref->n) != NULL) {
	if (!(ref->Flags & ErasedMask))
	  break;
      }
      /* we have used the DB entry, so we can remove it now, although
	 first we have to make sure noone is pointing to it */
      if (ref == NULL) {
	READ_UNLOCK(ref0->Parent->DBRWLock);
	cut_fail();
      }
    }
    else
      {
	READ_UNLOCK(ref0->Parent->DBRWLock);
	cut_fail();
      }
  }
	
  {
    Term ttmp = EXTRA_CBACK_ARG(3,2);
    if (IsLongIntTerm(ttmp))
      mask = (CELL)LongIntOfTerm(ttmp);
    else
      mask = (CELL)IntOfTerm(ttmp);
  }
  {
    Term ttmp = EXTRA_CBACK_ARG(3,3);
    if (IsLongIntTerm(ttmp))
      key = (CELL)LongIntOfTerm(ttmp);
    else
      key = (CELL)IntOfTerm(ttmp);
  }
  while (ref != NIL
	 && DEAD_REF(ref))
    ref = NextDBRef(ref);
  if (ref == NIL) {
    READ_UNLOCK(ref0->Parent->DBRWLock);
    cut_fail();
  }
  if (mask == 0 && key == 0) {	/* ARG2 is a variable */
    while ((TermDB = GetDBTermFromDBEntry(ref)) == (CELL)0) {
      /* make sure the garbage collector sees what we want it to see! */
      EXTRA_CBACK_ARG(3,1) = (CELL)ref;
      /* oops, we are in trouble, not enough stack space */
      if (!Yap_gcl(Yap_Error_Size, 3, ENV, CP)) {
	Yap_Error(OUT_OF_STACK_ERROR, TermNil, Yap_ErrorMessage);
	return(FALSE);
      }
      PreviousHeap = H;
    }
    Yap_unify(ARG2, TermDB);
  } else if (mask == 0) {	/* ARG2 is a constant */
    do {
      if (((key == Unsigned(ref->DBT.Entry)) || (ref->Flags & DBVar)) &&
	  !DEAD_REF(ref))
	break;
      ref = NextDBRef(ref);
    } while (ref != NIL);
    if (ref == NIL) {
      READ_UNLOCK(ref0->Parent->DBRWLock);
      cut_fail();
    }
  } else
    do {		/* ARG2 is a structure */
      H = PreviousHeap;
      while ((mask & ref->Key) != (key & ref->Mask)) {
	while ((ref = NextDBRef(ref)) != NIL
	       && DEAD_REF(ref));
	if (ref == NIL) {
	  READ_UNLOCK(ref0->Parent->DBRWLock);
	  cut_fail();
	}
      }
      while ((TermDB = GetDBTermFromDBEntry(ref)) == (CELL)0) {
	/* make sure the garbage collector sees what we want it to see! */
	EXTRA_CBACK_ARG(3,1) = (CELL)ref;
	/* oops, we are in trouble, not enough stack space */
	if (!Yap_gcl(Yap_Error_Size, 3, ENV, CP)) {
	  Yap_Error(OUT_OF_STACK_ERROR, TermNil, Yap_ErrorMessage);
	  return(FALSE);
	}
	PreviousHeap = H;
      }
      if (Yap_unify(ARG2, TermDB))
	break;
      while ((ref = NextDBRef(ref)) != NIL
	     && DEAD_REF(ref));
      if (ref == NIL) {
	READ_UNLOCK(ref0->Parent->DBRWLock);
	cut_fail();
      }
    } while (1);
  READ_UNLOCK(ref0->Parent->DBRWLock);
  TRef = MkDBRefTerm(ref);
  EXTRA_CBACK_ARG(3,1) = (CELL)ref;
#if defined(YAPOR) || defined(THREADS)
  LOCK(ref->lock);
  TRAIL_REF(ref);	/* So that fail will erase it */
  INC_DBREF_COUNT(ref);
  UNLOCK(ref->lock);
#else 
  if (!(ref->Flags & InUseMask)) {
    ref->Flags |= InUseMask;
    TRAIL_REF(ref);	/* So that fail will erase it */
  }
#endif
  return (Yap_unify(ARG3, TRef));
}

/*
 * The arguments for this 4 functions are the flags for terms which should be
 * skipped 
 */

static Int
lu_recorded(PredEntry *pe) {
  op_numbers opc = Yap_op_from_opcode(P->opc);

  if (opc == _procceed) {
    P = pe->CodeOfPred;
  } else {
    CP = P;
    P = pe->CodeOfPred;
    ENV = YENV;
    YENV = ASP;
    YENV[E_CB] = (CELL) B;
  }
  if (pe->PredFlags & ProfiledPredFlag) {
    LOCK(pe->StatisticsForPred.lock);
    pe->StatisticsForPred.NOfEntries++;
    UNLOCK(pe->StatisticsForPred.lock);
  }
  return TRUE;
}

/* recorded(+Functor,+Term,-Ref) */
static Int 
in_rded_with_key(void)
{
  DBProp AtProp = (DBProp)IntegerOfTerm(Deref(ARG1));

  return (i_recorded(AtProp,Deref(ARG3)));
}

/* recorded(+Functor,+Term,-Ref) */
static Int 
p_recorded(void)
{
  DBProp          AtProp;
  Register Term   twork = Deref(ARG1);	/* initially working with
					 * ARG1 */
  Term t3 = Deref(ARG3);
  PredEntry *pe;

  if (!IsVarTerm(t3)) {
    DBRef ref = DBRefOfTerm(t3);
    if (!IsDBRefTerm(t3)) {
      return FALSE;
    } else {
      ref = DBRefOfTerm(t3);
    }
    ref = DBRefOfTerm(t3);
    if (ref == NULL) return FALSE;
    if (DEAD_REF(ref)) {
      return FALSE;
    }
    if (ref->Flags & LogUpdMask) {
      LogUpdClause *cl = (LogUpdClause *)ref;
      PredEntry *ap;
      if (Yap_op_from_opcode(cl->ClCode->opc) == _unify_idb_term) {
	if (!Yap_unify(ARG2, cl->ClSource->Entry)) {
	  return FALSE;
	}
      } else if (!Yap_unify(ARG2,GetDBTerm(cl->ClSource))) {
	return FALSE;
      }
      ap = cl->ClPred;
      return Yap_unify(GetDBLUKey(ap), ARG1);
    } else if (!Yap_unify(ARG2,GetDBTermFromDBEntry(ref))
	       || !UnifyDBKey(ref,0,ARG1)) {
      return FALSE;
    } else {
      return TRUE;
    }
  }
  if ((pe = find_lu_entry(twork)) != NULL) {
    return lu_recorded(pe);
  }
  if (EndOfPAEntr(AtProp = FetchDBPropFromKey(twork, 0, FALSE, "recorded/3"))) {
    return FALSE;
  }
  ARG1 = MkIntegerTerm((Int)AtProp);
  P = PredRecordedWithKey->CodeOfPred;
  return (i_recorded(AtProp, t3));
}

static Int 
co_rded(void)
{
  return (c_recorded(0));
}

/* '$recordedp'(+Functor,+Term,-Ref) */
static Int 
in_rdedp(void)
{
  DBProp          AtProp;
  register choiceptr b0=B;
  Register Term   twork = Deref(ARG1);	/* initially working with
					 * ARG1 */

  Term t3 = Deref(ARG3);
  if (!IsVarTerm(t3)) {
    if (!IsDBRefTerm(t3)) {
      cut_fail();
    } else {
      DBRef ref = DBRefOfTerm(t3);
      LOCK(ref->lock);
      if (ref == NULL
	  || DEAD_REF(ref)
	  || !Yap_unify(ARG2,GetDBTermFromDBEntry(ref))
	  || !UnifyDBKey(ref,CodeDBBit,ARG1)) {
	UNLOCK(ref->lock);
	cut_fail();
      } else {
	UNLOCK(ref->lock);
	cut_succeed();
      }
    }
  }
  if (EndOfPAEntr(AtProp = FetchDBPropFromKey(twork, MkCode, FALSE, "recorded/3"))) {
    if (b0 == B)
      cut_fail();
    else
      return(FALSE);
  }
  return (i_recorded(AtProp,t3));
}


static Int 
co_rdedp(void)
{
  return (c_recorded(MkCode));
}

/* '$some_recordedp'(Functor)				 */
static Int 
p_somercdedp(void)
{
  Register DBRef  ref;
  DBProp            AtProp;
  Register Term   twork = Deref(ARG1);	/* initially working with
						 * ARG1 */
  if (EndOfPAEntr(AtProp = FetchDBPropFromKey(twork, MkCode, FALSE, "some_recorded/3"))) {
    return(FALSE);
  }
  READ_LOCK(AtProp->DBRWLock);
  ref = FrstDBRef(AtProp);
  while (ref != NIL && (ref->Flags & (DBNoCode | ErasedMask)))
    ref = NextDBRef(ref);
  READ_UNLOCK(AtProp->DBRWLock);
  if (ref == NIL)
    return (FALSE);
  else
    return (TRUE);
}

/* Finds the first instance recorded under key ARG1			 */
static Int 
p_first_instance(void)
{
  Term            TRef;
  Register DBRef  ref;
  DBProp          AtProp;
  Register Term   twork = Deref(ARG1);	/* initially working with
					 * ARG1 */
  Term TermDB;

  ARG3 = Deref(ARG3);
  if (!IsVarTerm(ARG3)) {
    cut_fail();
  }
  if (EndOfPAEntr(AtProp = FetchDBPropFromKey(twork, 0, FALSE, "first_instance/3"))) {
    return(FALSE);
  }
  READ_LOCK(AtProp->DBRWLock);
  ref = AtProp->First;
  while (ref != NIL
	 && (ref->Flags & (DBCode | ErasedMask)))
    ref = NextDBRef(ref);
  READ_UNLOCK(AtProp->DBRWLock);
  if (ref == NIL) {
    cut_fail();
  }
  TRef = MkDBRefTerm(ref);
  /* we have a pointer to the term available */
#if defined(YAPOR) || defined(THREADS)
  LOCK(ref->lock);
  TRAIL_REF(ref);	/* So that fail will erase it */
  INC_DBREF_COUNT(ref);
  UNLOCK(ref->lock);
#else 
  if (!(ref->Flags & InUseMask)) {
    ref->Flags |= InUseMask;
    TRAIL_REF(ref);	/* So that fail will erase it */
  }
#endif
  while ((TermDB = GetDBTermFromDBEntry(ref)) == (CELL)0) {
    /* oops, we are in trouble, not enough stack space */
    if (!Yap_gcl(Yap_Error_Size, 3, ENV, P)) {
      Yap_Error(OUT_OF_STACK_ERROR, TermNil, Yap_ErrorMessage);
      return(FALSE);
    }
  }
  if (IsVarTerm(TermDB)) {
    Yap_unify(TermDB, ARG2);
  } else {
    return(Yap_unify(ARG2, TermDB));
  }
  return(Yap_unify(ARG3, TRef));
}

static UInt
index_sz(LogUpdIndex *x)
{
  UInt sz = x->ClSize;
  x = x->ChildIndex;
  while (x != NULL) {
    sz += index_sz(x);
    x = x->SiblingIndex;
  }
  return sz;
}

static Int
lu_statistics(PredEntry *pe)
{
  UInt sz = 0, cls = 0, isz = 0;
  
  /* count number of clauses and size */
  LogUpdClause *x;

  if (pe->cs.p_code.FirstClause == NULL) {
    cls = 0;
    sz = 0;
  } else {
    x = ClauseCodeToLogUpdClause(pe->cs.p_code.FirstClause);
    while (x != NULL) {
      cls++;
      sz += x->ClSize;
      x = x->ClNext;
    }
  }
  if (pe->PredFlags & IndexedPredFlag) {
    isz = index_sz(ClauseCodeToLogUpdIndex(pe->cs.p_code.TrueCodeOfPred));
  } else {
    isz = 0;
  }
  return
    Yap_unify(ARG2,MkIntegerTerm(cls)) &&
    Yap_unify(ARG3,MkIntegerTerm(sz)) &&
    Yap_unify(ARG4,MkIntegerTerm(isz));
}


static Int
p_key_statistics(void)
{
  Register DBProp p;
  Register DBRef  x;
  UInt sz = 0, cls = 0;
  Term twork = Deref(ARG1);
  PredEntry *pe;

  if ((pe = find_lu_entry(twork)) != NULL) {
    return lu_statistics(pe);
  }
  if (EndOfPAEntr(p = FetchDBPropFromKey(twork, 0, TRUE, "key_statistics/3"))) {
    /* This is not a key property */
    return(FALSE);
  }
  /* count number of clauses and size */
  x = p->First;
  while (x != NULL) {
    cls++;
    sz += sizeof(DBStruct)+sizeof(CELL)*x->DBT.NOfCells;
    if (x->Code) {
      DynamicClause *cl = ClauseCodeToDynamicClause(x->Code);
      sz += cl->ClSize;      
    }
    x = NextDBRef(x);
  }
  return
    Yap_unify(ARG2,MkIntegerTerm(cls)) &&
    Yap_unify(ARG3,MkIntegerTerm(sz)) &&
    Yap_unify(ARG4,MkIntTerm(0));
}

#ifdef DEBUG
static Int
p_total_erased(void)
{
  UInt sz = 0, cls = 0;
  UInt isz = 0, icls = 0;
  LogUpdClause *cl = DBErasedList;
  LogUpdIndex *icl = DBErasedIList;

  /* only for log upds */
  while (cl) {
    cls++;
    sz += cl->ClSize;
    cl = cl->ClNext;
  }
  while (icl) {
    icls++;
    isz += icl->ClSize;
    icl = icl->SiblingIndex;
  }
  return
    Yap_unify(ARG1,MkIntegerTerm(cls)) &&
    Yap_unify(ARG2,MkIntegerTerm(sz)) &&
    Yap_unify(ARG3,MkIntegerTerm(icls)) &&
    Yap_unify(ARG4,MkIntegerTerm(isz));
}

static Int
p_key_erased_statistics(void)
{
  UInt sz = 0, cls = 0;
  UInt isz = 0, icls = 0;
  Term twork = Deref(ARG1);
  PredEntry *pe;
  LogUpdClause *cl = DBErasedList;
  LogUpdIndex *icl = DBErasedIList;

  /* only for log upds */
  if ((pe = find_lu_entry(twork)) == NULL) 
    return FALSE;
  while (cl) {
    if (cl->ClPred == pe) {
      cls++;
      sz += cl->ClSize;
    }
    cl = cl->ClNext;
  }
  while (icl) {
    LogUpdIndex *c = icl;

    while (!c->ClFlags & SwitchRootMask)
      c = c->u.ParentIndex;
    if (pe == c->u.pred) {
      icls++;
      isz += c->ClSize;
    }
    icl = icl->SiblingIndex;
  }
  return
    Yap_unify(ARG2,MkIntegerTerm(cls)) &&
    Yap_unify(ARG3,MkIntegerTerm(sz)) &&
    Yap_unify(ARG4,MkIntegerTerm(icls)) &&
    Yap_unify(ARG5,MkIntegerTerm(isz));
}

static Int
p_predicate_erased_statistics(void)
{
  UInt sz = 0, cls = 0;
  UInt isz = 0, icls = 0;
  Term twork = Deref(ARG1);
  PredEntry *pe;
  LogUpdClause *cl = DBErasedList;
  LogUpdIndex *icl = DBErasedIList;

  /* only for log upds */
  if ((pe = find_lu_entry(twork)) == NULL) 
    return FALSE;
  while (cl) {
    if (cl->ClPred == pe) {
      cls++;
      sz += cl->ClSize;
    }
    cl = cl->ClNext;
  }
  while (icl) {
    LogUpdIndex *c = icl;

    while (!c->ClFlags & SwitchRootMask)
      c = c->u.ParentIndex;
    if (pe == c->u.pred) {
      icls++;
      isz += c->ClSize;
    }
    icl = icl->SiblingIndex;
  }
  return
    Yap_unify(ARG2,MkIntegerTerm(cls)) &&
    Yap_unify(ARG3,MkIntegerTerm(sz)) &&
    Yap_unify(ARG4,MkIntegerTerm(icls)) &&
    Yap_unify(ARG5,MkIntegerTerm(isz));
}

static Int
p_heap_space_info(void)
{
  return
    Yap_unify(ARG1,MkIntegerTerm(HeapUsed)) &&
    Yap_unify(ARG2,MkIntegerTerm(HeapMax-HeapUsed));
}

#endif


/*
 * This is called when we are erasing a data base clause, because we may have
 * pending references 
 */
static void 
ErasePendingRefs(DBTerm *entryref)
{
  DBRef          *cp;
  DBRef           ref;

  cp = entryref->DBRefs;
  if (entryref->DBRefs == NULL)
    return;
  while ((ref = *--cp) != NULL) {
    if ((ref->Flags & DBClMask) && (--(ref->NOfRefsTo) == 0)
	&& (ref->Flags & ErasedMask))
      ErDBE(ref);
  }
}


inline static void 
RemoveDBEntry(DBRef entryref)
{

  ErasePendingRefs(&(entryref->DBT));
  /* We may be backtracking back to a deleted entry. If we just remove
     the space then the info on the entry may be corrupt.  */
  if ((B->cp_ap == RETRY_C_RECORDED_K_CODE 
       || B->cp_ap == RETRY_C_RECORDEDP_CODE) &&
      EXTRA_CBACK_ARG(3,1) == (CELL)entryref) {
    /* make it clear the entry has been released */
#if defined(YAPOR) || defined(THREADS)
    DEC_DBREF_COUNT(entryref);
#else 
    entryref->Flags &= ~InUseMask;
#endif
    DBErasedMarker->Next = NULL;
    DBErasedMarker->Parent = entryref->Parent;
    DBErasedMarker->n = entryref->n;
    EXTRA_CBACK_ARG(3,1) = (CELL)DBErasedMarker;
  }
  if (entryref->p != NULL)
    entryref->p->n = entryref->n;
  else 
    entryref->Parent->F0 = entryref->n;
  if (entryref->n != NULL)
    entryref->n->p = entryref->p;
  else 
    entryref->Parent->L0 = entryref->p;
  FreeDBSpace((char *) entryref);
}

static void
clean_lu_index(DBRef index) {
  DBRef *te = (DBRef *)(index->DBT.Contents);
  DBRef ref;

  LOCK(index->lock);
  if (DBREF_IN_USE(index)) {
    index->Flags |= ErasedMask;
    UNLOCK(index->lock);
    return;
  }
  while ((ref = *te++) != NULL) {
    LOCK(ref->lock);
    /* note that the first element of the conditional generates a
       side-effect, and should never be swapped around with the other */
    if ( --(ref->NOfRefsTo) == 0 && (ref->Flags & ErasedMask)) {
      if (!DBREF_IN_USE(ref)) {
	UNLOCK(ref->lock);
	RemoveDBEntry(ref);
      } else {
	UNLOCK(ref->lock);
      }
    } else {
      UNLOCK(ref->lock);
    }
  }
  UNLOCK(index->lock);
  /* can I get rid of this index? */
  FreeDBSpace((char *)index);
}



static yamop *
find_next_clause(DBRef ref0)
{
  Register DBRef  ref;
  yamop *newp;

  /* fetch ref0 from the instruction we just started executing */
#ifdef DEBUG
  if (!(ref0->Flags & ErasedMask)) {
    Yap_Error(SYSTEM_ERROR, TermNil, "find_next_clause (dead clause %x)", ref0);
    return(NIL);
  }
#endif
  /* search for an newer entry that is to the left and points to code */
  ref = ref0;
  while ((ref = ref->n) != NULL) {
    if (!(ref->Flags & ErasedMask))
      break;
  }
  /* no extra alternatives to try, let us leave gracefully */
  if (ref == NULL) {
    return(NULL);
  } else {
    /* OK, we found a clause we can jump to, do a bit of hanky pancking with
       the choice-point, so that it believes we are actually working from that
       clause */
    newp = ref->Code;
    /* and next let's tell the world this clause is being used, just
       like if we were executing a standard retry_and_mark */
#if defined(YAPOR) || defined(THREADS)
    {
      DynamicClause *cl = ClauseCodeToDynamicClause(newp);

      LOCK(cl->ClLock);
      TRAIL_CLREF(cl);
      INC_CLREF_COUNT(cl);
      UNLOCK(cl->ClLock);
    }
#else 
    if (!DynamicFlags(newp) & InUseMask) {
      DynamicFlags(newp) |= InUseMask;
      TRAIL_CLREF(ClauseCodeToDynamicClause(newp));
    }
#endif
    return(newp);
  }
}

/* This procedure is called when a clause is officialy deleted. Its job
   is to find out where the code can go next, if it can go anywhere */
static Int
p_jump_to_next_dynamic_clause(void)
{
  DBRef ref = (DBRef)(((yamop *)((CODEADDR)P-(CELL)NEXTOP((yamop *)NULL,sla)))->u.sla.bmap);
  yamop *newp = find_next_clause(ref);
  
  if (newp == NULL) {
    cut_fail();
  }
  /* the next alternative to try must be obtained from this clause */
  B->cp_ap = newp;
  /* and next, enter the clause */
  P = NEXTOP(newp,ld);
  /* and return like if nothing had happened. */
  return(TRUE);
}

static void
complete_lu_erase(LogUpdClause *clau)
{
  DBRef *cp;
  if (clau->ClSource)
    cp = clau->ClSource->DBRefs;
  else 
    cp = NULL;
  if (CL_IN_USE(clau)) {
    return;
  }
  if (clau->ClFlags & LogUpdRuleMask &&
      clau->ClExt->u.EC.ClRefs > 0) {
    return;
  }
#ifdef DEBUG
#ifndef THREADS
  if (clau->ClNext)
    clau->ClNext->ClPrev = clau->ClPrev;
  if (clau->ClPrev) {
    clau->ClPrev->ClNext = clau->ClNext;
  } else {
    DBErasedList = clau->ClNext;
  }
#endif
#endif
  if (cp != NULL) {
    DBRef ref;
    while ((ref = *--cp) != NIL) {
      if (ref->Flags & LogUpdMask) {
	LogUpdClause *cl = (LogUpdClause *)ref;
	LOCK(cl->ClLock);
	cl->ClRefCount--;
	if (cl->ClFlags & ErasedMask &&
	    !(cl->ClFlags & InUseMask) &&
	    !(cl->ClRefCount)) {
	  UNLOCK(cl->ClLock);
	  EraseLogUpdCl(cl);
	} else {
	  UNLOCK(cl->ClLock);
	}
      } else {
	LOCK(ref->lock);
	ref->NOfRefsTo--;
	if (ref->Flags & ErasedMask &&
	    !(ref->Flags & InUseMask) &&
	    ref->NOfRefsTo) {
	  UNLOCK(ref->lock);
	  ErDBE(ref);
	} else {
	  UNLOCK(ref->lock);
	}
      }
    }
  }
  Yap_FreeCodeSpace((char *)clau);
}

static void
EraseLogUpdCl(LogUpdClause *clau)
{
  PredEntry *ap;

  ap = clau->ClPred;
  LOCK(clau->ClLock);
  /* no need to erase what has been erased */ 
  if (!(clau->ClFlags & ErasedMask)) {
#if defined(YAPOR) || defined(THREADS)
    int i_locked = FALSE;

    if (WPP != ap) {
      WRITE_LOCK(ap->PRWLock);
      if (WPP == NULL) {
	i_locked = TRUE;
	WPP = ap;
      }
    }
#endif
    /* get ourselves out of the list */
    if (clau->ClNext != NULL) {
      LOCK(clau->ClNext->ClLock);
      clau->ClNext->ClPrev = clau->ClPrev;
      UNLOCK(clau->ClNext->ClLock);
    }
    if (clau->ClPrev != NULL) {
      LOCK(clau->ClPrev->ClLock);
      clau->ClPrev->ClNext = clau->ClNext;
      UNLOCK(clau->ClPrev->ClLock);
    }
    UNLOCK(clau->ClLock);
    if (clau->ClCode == ap->cs.p_code.FirstClause) {
      if (clau->ClNext == NULL) {
	ap->cs.p_code.FirstClause = NULL;
      } else {
	ap->cs.p_code.FirstClause = clau->ClNext->ClCode;
      }
    }
    if (clau->ClCode == ap->cs.p_code.LastClause) {
      if (clau->ClPrev == NULL) {
	ap->cs.p_code.LastClause = NULL;
      } else {
	ap->cs.p_code.LastClause = clau->ClPrev->ClCode;
      }
    }
    ap->cs.p_code.NOfClauses--;
    clau->ClFlags |= ErasedMask;
#ifdef DEBUG
#ifndef THREADS
    {
      LogUpdClause *er_head = DBErasedList;
      if (er_head == NULL) {
	clau->ClPrev = clau->ClNext = NULL;
      } else {
	clau->ClNext = er_head;
	er_head->ClPrev = clau;
	clau->ClPrev = NULL;
      }
      DBErasedList = clau;
    }
#endif
#endif
    /* we are holding a reference to the clause */
    clau->ClRefCount++;
    UNLOCK(clau->ClLock);
    Yap_RemoveClauseFromIndex(ap, clau->ClCode);
    /* release the extra reference */
    LOCK(clau->ClLock);
    clau->ClRefCount--;
#if defined(YAPOR) || defined(THREADS)
    if (WPP != ap || i_locked) {
      if (i_locked) WPP= NULL;
      WRITE_UNLOCK(ap->PRWLock);
    }
#endif
  }
  UNLOCK(clau->ClLock);
  complete_lu_erase(clau);
}

static void
MyEraseClause(DynamicClause *clau)
{
  DBRef           ref;
  SMALLUNSGN      clmask;

  if (CL_IN_USE(clau))
    return;
  clmask = clau->ClFlags;
  /*
    I don't need to lock the clause at this point because 
    I am the last one using it anyway.
  */
  ref = (DBRef) NEXTOP(clau->ClCode,ld)->u.sla.bmap;
  /* don't do nothing if the reference is still in use */
  if (DBREF_IN_USE(ref))
    return;
  if ( P == clau->ClCode ) {
    yamop *np = RTRYCODE;
    /* make it the next alternative */
    np->u.ld.d = find_next_clause((DBRef)(NEXTOP(P,ld)->u.sla.bmap));
    if (np->u.ld.d == NULL)
      P = (yamop *)FAILCODE;
    else {
      /* with same arity as before */
      np->u.ld.s = P->u.ld.s;
      np->u.ld.p = P->u.ld.p;
      /* go ahead and try this code */
      P = np;
    }
  } else {
    Yap_FreeCodeSpace((char *)clau);
#ifdef DEBUG
    if (ref->NOfRefsTo)
      fprintf(Yap_stderr, "Error: references to dynamic clause\n");
#endif
    RemoveDBEntry(ref);
  }
}

/*
  This predicate is supposed to be called with a
  lock on the current predicate
*/
void 
Yap_ErLogUpdCl(LogUpdClause *clau)
{
  EraseLogUpdCl(clau);
}

/*
  This predicate is supposed to be called with a
  lock on the current predicate
*/
void 
Yap_ErCl(DynamicClause *clau)
{
  MyEraseClause(clau);
}

#define TRYCODE(G,F,N) ( (N)<5 ? (op_numbers)((int)(F)+(N)*3) : G)

static void 
PrepareToEraseLogUpdClause(LogUpdClause *clau, DBRef dbr)
{
  yamop          *code_p = clau->ClCode;
  PredEntry *p = clau->ClPred;
  yamop *cl = code_p;

  if (clau->ClFlags & ErasedMask)
    return;
  clau->ClFlags |= ErasedMask;
#if defined(YAPOR) || defined(THREADS)
  if (WPP != p) {
    WRITE_LOCK(p->PRWLock);
  }
#endif
  if (p->cs.p_code.FirstClause != cl) {
    /* we are not the first clause... */
    yamop *prev_code_p = (yamop *)(dbr->Prev->Code);
    prev_code_p->u.ld.d = code_p->u.ld.d; 
    /* are we the last? */
    if (p->cs.p_code.LastClause == cl)
      p->cs.p_code.LastClause = prev_code_p;
  } else {
    /* we are the first clause, what about the last ? */
    if (p->cs.p_code.LastClause == p->cs.p_code.FirstClause) {
      p->cs.p_code.LastClause = p->cs.p_code.FirstClause = NULL;
    } else {
      p->cs.p_code.FirstClause = code_p->u.ld.d;
      p->cs.p_code.FirstClause->opc =
       Yap_opcode(TRYCODE(_try_me, _try_me0, p->ArityOfPE));
    }
  }
  dbr->Code = NULL;   /* unlink the two now */
  if (p->PredFlags & IndexedPredFlag) {
    p->cs.p_code.NOfClauses--;
    Yap_RemoveIndexation(p);
  } else {
    EraseLogUpdCl(clau);
  }
  if (p->cs.p_code.FirstClause == p->cs.p_code.LastClause) {
    if (p->cs.p_code.FirstClause != NULL) {
      code_p = p->cs.p_code.FirstClause;
      code_p->u.ld.d = p->cs.p_code.FirstClause;
      p->cs.p_code.TrueCodeOfPred = NEXTOP(code_p, ld);
      if (p->PredFlags & SpiedPredFlag) {
	p->OpcodeOfPred = Yap_opcode(_spy_pred);
	p->CodeOfPred = (yamop *)(&(p->OpcodeOfPred)); 
      } else {
	p->CodeOfPred = p->cs.p_code.TrueCodeOfPred;
	p->OpcodeOfPred = p->cs.p_code.TrueCodeOfPred->opc;
      }
    } else {
      p->OpcodeOfPred = FAIL_OPCODE;
      p->cs.p_code.TrueCodeOfPred = p->CodeOfPred = (yamop *)(&(p->OpcodeOfPred)); 
    }
  } else {
    if (p->PredFlags & SpiedPredFlag) {
      p->OpcodeOfPred = Yap_opcode(_spy_pred);
      p->CodeOfPred = (yamop *)(&(p->OpcodeOfPred)); 
    } else {
      p->OpcodeOfPred = INDEX_OPCODE;
      p->CodeOfPred = (yamop *)(&(p->OpcodeOfPred)); 
    }
  }
#if defined(YAPOR) || defined(THREADS)
  if (WPP != p) {
    WRITE_UNLOCK(p->PRWLock);
  }
#endif
}

static void 
PrepareToEraseClause(DynamicClause *clau, DBRef dbr)
{
}

static void 
ErDBE(DBRef entryref)
{

  if ((entryref->Flags & DBCode) && entryref->Code) {
    if (entryref->Flags & LogUpdMask) {
      LogUpdClause *clau = ClauseCodeToLogUpdClause(entryref->Code);
      LOCK(clau->ClLock);
      if (CL_IN_USE(clau) || entryref->NOfRefsTo != 0) {
	PrepareToEraseLogUpdClause(clau, entryref);
	UNLOCK(clau->ClLock);
      } else {
	if (!(clau->ClFlags & ErasedMask))
	  PrepareToEraseLogUpdClause(clau, entryref);
	UNLOCK(clau->ClLock);
	/* the clause must have left the chain */
	EraseLogUpdCl(clau);
      }
    } else {
      DynamicClause *clau = ClauseCodeToDynamicClause(entryref->Code);
      LOCK(clau->ClLock);
      if (CL_IN_USE(clau) || entryref->NOfRefsTo != 0) {
	PrepareToEraseClause(clau, entryref);
	UNLOCK(clau->ClLock);
      } else {
	if (!(clau->ClFlags & ErasedMask))
	  PrepareToEraseClause(clau, entryref);
	UNLOCK(clau->ClLock);
	/* the clause must have left the chain */
	MyEraseClause(clau);
      }
    }
  } else if (!(DBREF_IN_USE(entryref))) {
    if (entryref->NOfRefsTo == 0) 
      RemoveDBEntry(entryref);
    else if (!(entryref->Flags & ErasedMask)) {
      /* oops, I cannot remove it, but I at least have to tell
	 the world what's going on */
      entryref->Flags |= ErasedMask;
      entryref->Next = entryref->Prev = NIL;
    }
  }
}

void 
Yap_ErDBE(DBRef entryref)
{
  ErDBE(entryref);
}

static void
EraseEntry(DBRef entryref)
{
  DBProp          p;

  if (entryref->Flags & ErasedMask)
    return;
  if (entryref->Flags & StaticMask) {
    return;
  }
  if (entryref->Flags & LogUpdMask &&
      !(entryref->Flags & DBClMask)) {
    EraseLogUpdCl((LogUpdClause *)entryref);
    return;
  }
  entryref->Flags |= ErasedMask;
  /* update FirstNEr */
  p = entryref->Parent;
  if (p->KindOfPE & LogUpdDBBit) {
    LogUpdDBProp lup = (LogUpdDBProp)p;
    lup->NOfEntries--;
    if (lup->Index != NULL) {
      clean_lu_index(lup->Index);
      lup->Index = NULL;
    }
  }
  /* exit the db chain */
  if (entryref->Next != NIL) {
    entryref->Next->Prev = entryref->Prev;
  } else {
    p->Last = entryref->Prev;
  }
  if (entryref->Prev != NIL)
    entryref->Prev->Next = entryref->Next;
  else
    p->First = entryref->Next;
  /* make sure we know the entry has been removed from the list */
  entryref->Next = NIL;
  if (!DBREF_IN_USE(entryref)) {
    ErDBE(entryref);
  } else if ((entryref->Flags & DBCode) && entryref->Code) {
    if (p->KindOfPE & LogUpdDBBit) {
      PrepareToEraseLogUpdClause(ClauseCodeToLogUpdClause(entryref->Code), entryref);
    } else {
      PrepareToEraseClause(ClauseCodeToDynamicClause(entryref->Code), entryref);
    }
  }
}

/* erase(+Ref)	 */
static Int 
p_erase(void)
{
  Term t1 = Deref(ARG1);

  if (IsVarTerm(t1)) {
    Yap_Error(INSTANTIATION_ERROR, t1, "erase");
    return (FALSE);
  }
  if (!IsDBRefTerm(t1)) {
    Yap_Error(TYPE_ERROR_DBREF, t1, "erase");
    return (FALSE);
  }
  EraseEntry(DBRefOfTerm(t1));
  return (TRUE);
}

static Int
p_erase_clause(void)
{
  Term t1 = Deref(ARG1);
  DBRef entryref;

  if (IsVarTerm(t1)) {
    Yap_Error(INSTANTIATION_ERROR, t1, "erase");
    return (FALSE);
  }
  if (!IsDBRefTerm(t1)) {
    Yap_Error(TYPE_ERROR_DBREF, t1, "erase");
    return (FALSE);
  } else {
    entryref = DBRefOfTerm(t1);
  }
  if (entryref->Flags & StaticMask) {
    if (entryref->Flags & ErasedMask)
      return FALSE;
    Yap_EraseStaticClause((StaticClause *)entryref, Deref(ARG2));
    return TRUE;
  }
  EraseEntry(entryref);
  return TRUE;
}
 
/* eraseall(+Key)	 */
static Int 
p_eraseall(void)
{
  Register Term   twork = Deref(ARG1);
  Register DBRef  entryref;
  DBProp          p;
  PredEntry *pe;

  if ((pe = find_lu_entry(twork)) != NULL) {
    LogUpdClause *cl;

    if (!pe->cs.p_code.NOfClauses)
      return TRUE;
    if (pe->PredFlags & IndexedPredFlag)
      Yap_RemoveIndexation(pe);
    cl = ClauseCodeToLogUpdClause(pe->cs.p_code.FirstClause);
    do {
      LogUpdClause *ncl = cl->ClNext;
      Yap_ErLogUpdCl(cl);
      cl = ncl;
    } while (cl != NULL);
    return TRUE;
  }
  if (EndOfPAEntr(p = FetchDBPropFromKey(twork, 0, FALSE, "eraseall/3"))) {
    return(TRUE);
  }
  WRITE_LOCK(p->DBRWLock);
  if (p->KindOfPE & LogUpdDBBit) {
    LogUpdDBProp lup = (LogUpdDBProp)p;
    lup->NOfEntries = 0;
    if (lup->Index != NULL) {
      clean_lu_index(lup->Index);
      lup->Index = NULL;
    }
  }
  entryref = FrstDBRef(p);
  do {
    DBRef next_entryref;

    while (entryref != NIL &&
	   (entryref->Flags & (DBCode | ErasedMask)))
      entryref = NextDBRef(entryref);
    if (entryref == NIL)
      break;
    next_entryref = NextDBRef(entryref);
    /* exit the db chain */
    if (entryref->Next != NIL) {
      entryref->Next->Prev = entryref->Prev;
    } else {
      p->Last = entryref->Prev;
    }
    if (entryref->Prev != NIL)
      entryref->Prev->Next = entryref->Next;
    else
      p->First = entryref->Next;
    /* make sure we know the entry has been removed from the list */
    entryref->Next = entryref->Prev = NIL;
    if (!DBREF_IN_USE(entryref))
      ErDBE(entryref);
    else {
      entryref->Flags |= ErasedMask;
    }
    entryref = next_entryref;
  } while (entryref != NIL);
  WRITE_UNLOCK(p->DBRWLock);
  return (TRUE);
}


/* erased(+Ref) */
static Int 
p_erased(void)
{
  Term            t = Deref(ARG1);

  if (IsVarTerm(t)) {
    Yap_Error(INSTANTIATION_ERROR, t, "erased");
    return (FALSE);
  }
  if (!IsDBRefTerm(t)) {
    Yap_Error(TYPE_ERROR_DBREF, t, "erased");
    return (FALSE);
  }
  return (DBRefOfTerm(t)->Flags & ErasedMask);
}

static Int
static_instance(StaticClause *cl)
{
  if (cl->ClFlags & ErasedMask) {
    return FALSE;
  }
  if (cl->ClFlags & FactMask) {
    PredEntry *ap = cl->usc.ClPred;
    if (ap->ArityOfPE == 0) {
      return Yap_unify(ARG2,MkAtomTerm((Atom)ap->FunctorOfPred));
    } else {
      Functor f = ap->FunctorOfPred;
      UInt arity = ArityOfFunctor(ap->FunctorOfPred), i;
      Term t2 = Deref(ARG2);
      CELL *ptr;

      if (IsVarTerm(t2)) {
	Yap_unify(ARG2, (t2 = Yap_MkNewApplTerm(f,arity)));
      } else if (!IsApplTerm(t2) || FunctorOfTerm(t2) != f) {
	return FALSE;
      }
      ptr = RepAppl(t2)+1;
      for (i=0; i<arity; i++) {
	XREGS[i+1] = ptr[i];
      }
      CP = P;
      YENV = ASP;
      YENV[E_CB] = (CELL) B;
      P = cl->ClCode;
      return TRUE;
    }
  } else {
    Term TermDB;

    while ((TermDB = GetDBTerm(cl->usc.ClSource)) == 0L) {
      /* oops, we are in trouble, not enough stack space */
      if (!Yap_gcl(Yap_Error_Size, 2, ENV, P)) {
	Yap_Error(OUT_OF_STACK_ERROR, TermNil, Yap_ErrorMessage);
	return(FALSE);
      }
    }
    return Yap_unify(ARG2, TermDB);
  }
}

/* instance(+Ref,?Term) */
static Int 
p_instance(void)
{
  Term t1 = Deref(ARG1);
  DBRef dbr;

  if (IsVarTerm(t1) || !IsDBRefTerm(t1)) {
    return (FALSE);
  } else {
    dbr = DBRefOfTerm(t1);
  }
  if (dbr->Flags & StaticMask) {
    return static_instance((StaticClause *)dbr);
  } else if (dbr->Flags & LogUpdMask) {
    op_numbers opc;
    LogUpdClause *cl = (LogUpdClause *)dbr;

    if (cl->ClFlags & ErasedMask) {
      return FALSE;
    }
    if (cl->ClSource == NULL) {
      PredEntry *ap = cl->ClPred;
      if (ap->ArityOfPE == 0) {
	return Yap_unify(ARG2,MkAtomTerm((Atom)ap->FunctorOfPred));
      } else {
	Functor f = ap->FunctorOfPred;
	UInt arity = ArityOfFunctor(ap->FunctorOfPred), i;
	Term t2 = Deref(ARG2);
	CELL *ptr;

	if (IsVarTerm(t2)) {
	  Yap_unify(ARG2, (t2 = Yap_MkNewApplTerm(f,arity)));
	} else if (!IsApplTerm(t2) || FunctorOfTerm(t2) != f) {
	  return FALSE;
	}
	ptr = RepAppl(t2)+1;
	for (i=0; i<arity; i++) {
	  XREGS[i+1] = ptr[i];
	}
	CP = P;
	YENV = ASP;
	YENV[E_CB] = (CELL) B;
	P = cl->ClCode;
	return TRUE;
      }
    }
    opc = Yap_op_from_opcode(cl->ClCode->opc);
    if (opc == _unify_idb_term) {
      return Yap_unify(ARG2, cl->ClSource->Entry);
    } else  {
      Term            TermDB;
      while ((TermDB = GetDBTerm(cl->ClSource)) == 0L) {
	/* oops, we are in trouble, not enough stack space */
	if (!Yap_gcl(Yap_Error_Size, 2, ENV, P)) {
	  Yap_Error(OUT_OF_STACK_ERROR, TermNil, Yap_ErrorMessage);
	  return(FALSE);
	}
      }
      return Yap_unify(ARG2, TermDB);
    }
  } else {
    Term            TermDB;
    while ((TermDB = GetDBTermFromDBEntry(dbr)) == 0L) {
      /* oops, we are in trouble, not enough stack space */
      if (!Yap_gcl(Yap_Error_Size, 2, ENV, P)) {
	Yap_Error(OUT_OF_STACK_ERROR, TermNil, Yap_ErrorMessage);
	return(FALSE);
      }
      t1 = Deref(ARG1);
    }
    return Yap_unify(ARG2, TermDB);
  }
}

/* instance(+Ref,?Term) */
static Int 
p_instance_module(void)
{
  Term t1 = Deref(ARG1);
  DBRef dbr;

  if (IsVarTerm(t1)) {
    return FALSE;
  }
  if (IsDBRefTerm(t1)) {
    dbr = DBRefOfTerm(t1);
  } else {
    return FALSE;
  }
  if (dbr->Flags & LogUpdMask) {
    LogUpdClause *cl = (LogUpdClause *)dbr;

    if (cl->ClFlags & ErasedMask) {
      return FALSE;
    }
    if (cl->ClPred->ModuleOfPred)
      return Yap_unify(ARG2, cl->ClPred->ModuleOfPred);
    else
      return Yap_unify(ARG2, TermProlog);
  } else {
    return Yap_unify(ARG2, dbr->Parent->ModuleOfDB);
  }
}

inline static int 
NotActiveDB(DBRef my_dbref)
{
  while (my_dbref && (my_dbref->Flags & (DBCode | ErasedMask)))
    my_dbref = my_dbref->Next;
  return (my_dbref == NIL);
}

inline static DBEntry *
NextDBProp(PropEntry *pp)
{
  while (!EndOfPAEntr(pp) && (((pp->KindOfPE & ~ 0x1) != DBProperty) ||
			      NotActiveDB(((DBProp) pp)->First)))
    pp = RepProp(pp->NextOfPE);
  return ((DBEntry *)pp);
}

static Int 
init_current_key(void)
{				/* current_key(+Atom,?key)	 */
  Int             i = 0;
  DBEntry        *pp;
  Atom            a;
  Term t1 = ARG1;

  t1 = Deref(ARG1);
  if (!IsVarTerm(t1)) {
    if (IsAtomTerm(t1))
      a = AtomOfTerm(t1);
    else {
      cut_fail();
    }
  } else {
    /* ask for the first hash line */
    while (TRUE) {
      READ_LOCK(HashChain[i].AERWLock);
      a = HashChain[i].Entry;
      if (a != NIL) {
	break;
      }
      READ_UNLOCK(HashChain[i].AERWLock);
      i++;
    }
    READ_UNLOCK(HashChain[i].AERWLock);
  }
  READ_LOCK(RepAtom(a)->ARWLock);
  pp = NextDBProp(RepProp(RepAtom(a)->PropsOfAE));
  READ_UNLOCK(RepAtom(a)->ARWLock);
  EXTRA_CBACK_ARG(2,3) = MkAtomTerm(a);
  EXTRA_CBACK_ARG(2,2) = MkIntTerm(i);
  EXTRA_CBACK_ARG(2,1) = MkIntegerTerm((Int)pp);
  return (cont_current_key());
}

static Int 
cont_current_key(void)
{
  unsigned int    arity;
  Functor         functor;
  Term            term, AtT;
  Atom            a;
  Int             i = IntegerOfTerm(EXTRA_CBACK_ARG(2,2));
  Term            first = Deref(ARG1);
  DBEntry        *pp = (DBEntry *) IntegerOfTerm(EXTRA_CBACK_ARG(2,1));

  if (IsIntTerm(term = EXTRA_CBACK_ARG(2,3)))
    return(cont_current_key_integer());
  a = AtomOfTerm(term);
  if (EndOfPAEntr(pp) && IsAtomTerm(first)) {
    cut_fail();
  }
  while (EndOfPAEntr(pp)) {
    UInt j;

    if ((a = RepAtom(a)->NextOfAE) == NIL) {
      i++;
      while (i < AtomHashTableSize) {
	/* protect current hash table line, notice that the current
	   LOCK/UNLOCK algorithm assumes new entries are added to
	   the *front* of the list, otherwise I should have locked
	   earlier.
	*/
	READ_LOCK(HashChain[i].AERWLock);
	a = HashChain[i].Entry;
	if (a != NIL) {
	  break;
	}
	/* move to next entry */
	READ_UNLOCK(HashChain[i].AERWLock);
	i++;
      }
      if (i == AtomHashTableSize) {
	/* we have left the atom hash table */
	/* we don't have a lock over the hash table any longer */
	if (IsAtomTerm(first)) {
	  cut_fail();
	}
	j = 0;
	if (INT_KEYS == NULL) {
	  cut_fail();
	}
	for(j = 0; j < INT_KEYS_SIZE; j++) {
	  if (INT_KEYS[j] != NIL) {
	    DBProp          pptr = RepDBProp(INT_KEYS[j]);
	    EXTRA_CBACK_ARG(2,1) = MkIntegerTerm((Int)(pptr->NextOfPE));
	    EXTRA_CBACK_ARG(2,2) = MkIntegerTerm(j+1);
	    EXTRA_CBACK_ARG(2,3) = MkIntTerm(INT_KEYS_TIMESTAMP);
	    term = MkIntegerTerm((Int)(pptr->FunctorOfDB));
	    return(Yap_unify(term,ARG1) && Yap_unify(term,ARG2));
	  }
	}
	if (j == INT_KEYS_SIZE) {
	  cut_fail();
	}	  
	return(cont_current_key_integer());
      } else {
	/* release our lock over the hash table */
	READ_UNLOCK(HashChain[i].AERWLock);
	EXTRA_CBACK_ARG(2,2) = MkIntTerm(i);
      }
    }
    READ_LOCK(RepAtom(a)->ARWLock);
    if (!EndOfPAEntr(pp = NextDBProp(RepProp(RepAtom(a)->PropsOfAE))))
      EXTRA_CBACK_ARG(2,3)  = (CELL) MkAtomTerm(a);
    READ_UNLOCK(RepAtom(a)->ARWLock);
  }
  READ_LOCK(RepAtom(a)->ARWLock);
  EXTRA_CBACK_ARG(2,1) = MkIntegerTerm((Int)NextDBProp(RepProp(pp->NextOfPE)));
  READ_UNLOCK(RepAtom(a)->ARWLock);
  arity = (unsigned int)(pp->ArityOfDB);
  if (arity == 0) {
    term = AtT = MkAtomTerm(a);
  } else {
    unsigned int j;
    CELL *p = H;

    for (j = 0; j < arity; j++) {
      p[j] = MkVarTerm();
    }
    functor = Yap_MkFunctor(a, arity);
    term = Yap_MkApplTerm(functor, arity, p);
    AtT = MkAtomTerm(a);
  }
  return (Yap_unify_constant(ARG1, AtT) && Yap_unify(ARG2, term));
}

static Int 
cont_current_key_integer(void)
{
  Term            term;
  UInt             i = IntOfTerm(EXTRA_CBACK_ARG(2,2));
  Prop            pp = (Prop)IntegerOfTerm(EXTRA_CBACK_ARG(2,1));
  UInt            tstamp = (UInt)IntOfTerm(EXTRA_CBACK_ARG(2,3));
  DBProp          pptr;

  if (tstamp != INT_KEYS_TIMESTAMP) {
    cut_fail();
  }
  while (pp == NIL) {
    for(;i < INT_KEYS_SIZE; i++) {
      if (INT_KEYS[i] != NIL) {
	EXTRA_CBACK_ARG(2,2) = MkIntTerm(i+1);
	pp = INT_KEYS[i];
	break;
      }
    }
    if (i == INT_KEYS_SIZE) {
      cut_fail();
    }
  }
  pptr = RepDBProp(pp);
  EXTRA_CBACK_ARG(2,1) = MkIntegerTerm((Int)(pptr->NextOfPE));
  term = MkIntegerTerm((Int)(pptr->FunctorOfDB));
  return(Yap_unify(term,ARG1) && Yap_unify(term,ARG2));
}

Term 
Yap_FetchTermFromDB(DBTerm *ref)
{
  return GetDBTerm(ref);
}

static DBTerm *
StoreTermInDB(Term t, int nargs)
{
  DBTerm *x;
  int needs_vars;
  struct db_globs dbg;

  s_dbg = &dbg;
  Yap_Error_Size = 0;
  while ((x = (DBTerm *)CreateDBStruct(t, (DBProp)NULL,
			  InQueue, &needs_vars, 0, &dbg)) == NULL) {
    switch(Yap_Error_TYPE) {
    case YAP_NO_ERROR:
#ifdef DEBUG
      Yap_Error(SYSTEM_ERROR, TermNil, "no error but null return in enqueue/2");
#endif
      break;
    case OUT_OF_STACK_ERROR:
      XREGS[nargs+1] = t;
      if (!Yap_gc(nargs+1, ENV, P)) {
	Yap_Error(OUT_OF_STACK_ERROR, TermNil, Yap_ErrorMessage);
	return(FALSE);
      } else {
	t = Deref(XREGS[nargs+1]);
	break;
      }
    case OUT_OF_TRAIL_ERROR:
      XREGS[nargs+1] = t;
      if(!Yap_growtrail (sizeof(CELL) * 16 * 1024L)) {
	Yap_Error(OUT_OF_TRAIL_ERROR, TermNil, "YAP could not grow trail in recorda/3");
	return FALSE;
      } else {
	t = Deref(XREGS[nargs+1]);
	break;
      }      
    case OUT_OF_HEAP_ERROR:
      XREGS[nargs+1] = t;
      if (!Yap_ExpandPreAllocCodeSpace(Yap_Error_Size)) {
	return FALSE;
      }
      t = Deref(XREGS[nargs+1]);
      break;
    default:
      Yap_Error(Yap_Error_TYPE, Yap_Error_Term, Yap_ErrorMessage);
      return(FALSE);
    }
  }
  return(x);
}

DBTerm *
Yap_StoreTermInDB(Term t, int nargs) {
  return StoreTermInDB(t, nargs);
}

DBTerm *
Yap_StoreTermInDBPlusExtraSpace(Term t, UInt extra_size) {
  int needs_vars;
  struct db_globs dbg;

  s_dbg = &dbg;
  return (DBTerm *)CreateDBStruct(t, (DBProp)NULL,
			  InQueue, &needs_vars, extra_size, &dbg);
}


static Int 
p_init_queue(void)
{
  db_queue *dbq;
  Term t;

  while ((dbq = (db_queue *)AllocDBSpace(sizeof(db_queue))) == NULL) {
    if (!Yap_growheap(FALSE, sizeof(db_queue), NULL)) {
      Yap_Error(OUT_OF_HEAP_ERROR, TermNil, "in findall");
      return(FALSE);
    }
  }
  dbq->id = FunctorDBRef;
  dbq->Flags = DBClMask;
  dbq->FirstInQueue = dbq->LastInQueue = NULL;
  INIT_RWLOCK(dbq->QRWLock);
  t = MkIntegerTerm((Int)dbq);
  return(Yap_unify(ARG1, t));
}


static Int 
p_enqueue(void)
{
  Term Father = Deref(ARG1);
  QueueEntry *x;
  db_queue *father_key;

  if (IsVarTerm(Father)) {
    Yap_Error(INSTANTIATION_ERROR, Father, "enqueue");
    return(FALSE);
  } else if (!IsIntegerTerm(Father)) {
    Yap_Error(TYPE_ERROR_INTEGER, Father, "enqueue");
    return(FALSE);
  } else
    father_key = (db_queue *)IntegerOfTerm(Father);
  while ((x = (QueueEntry *)AllocDBSpace(sizeof(QueueEntry))) == NULL) {
    if (!Yap_growheap(FALSE, sizeof(QueueEntry), NULL)) {
      Yap_Error(OUT_OF_HEAP_ERROR, TermNil, "in findall");
      return FALSE;
    }
  }
  x->DBT = StoreTermInDB(Deref(ARG2), 2);
  if (x->DBT == NULL)
    return FALSE;
  x->next = NULL;
  WRITE_LOCK(father_key->QRWLock);
  if (father_key->LastInQueue != NULL)
    father_key->LastInQueue->next = x;
  father_key->LastInQueue = x;
  if (father_key->FirstInQueue == NULL) {
    father_key->FirstInQueue = x;
  }
  WRITE_UNLOCK(father_key->QRWLock);
  return(TRUE);
}

/* when reading an entry in the data base we are making it accessible from
   the outside. If the entry was removed, and this was the last pointer, the
   target entry would be immediately removed, leading to dangling pointers.
   We avoid this problem by making every entry accessible. 

   Note that this could not happen with recorded, because the original db
   entry itself is still accessible from a trail entry, so we could not remove
   the target entry,
 */
static void
keepdbrefs(DBTerm *entryref)
{
  DBRef           *cp;
  DBRef           ref;

  cp = entryref->DBRefs;
  if (cp == NULL) {
    return;
  }
  while ((ref = *--cp) != NIL) {
    if (!(ref->Flags & LogUpdMask)) {
      LOCK(ref->lock);
      if(!(ref->Flags & InUseMask)) {
	ref->Flags |= InUseMask;
	TRAIL_REF(ref);	/* So that fail will erase it */
      }
      UNLOCK(ref->lock);
    }
  }

}

static Int 
p_dequeue(void)
{
  db_queue *father_key;
  QueueEntry *cur_instance;
  Term Father = Deref(ARG1);

  if (IsVarTerm(Father)) {
    Yap_Error(INSTANTIATION_ERROR, Father, "dequeue");
    return(FALSE);
  } else if (!IsIntegerTerm(Father)) {
    Yap_Error(TYPE_ERROR_INTEGER, Father, "dequeue");
    return(FALSE);
  } else
    father_key = (db_queue *)IntegerOfTerm(Father);
  WRITE_LOCK(father_key->QRWLock);
  if ((cur_instance = father_key->FirstInQueue) == NULL) {
    /* an empty queue automatically goes away */
    WRITE_UNLOCK(father_key->QRWLock);
    FreeDBSpace((char *)father_key);
    return(FALSE);
  } else {
    Term TDB;
    if (cur_instance == father_key->LastInQueue)
      father_key->FirstInQueue = father_key->LastInQueue = NULL;
    else
      father_key->FirstInQueue = cur_instance->next;
    WRITE_UNLOCK(father_key->QRWLock);
    while ((TDB = GetDBTerm(cur_instance->DBT)) == 0L) {
      if (!Yap_gcl(Yap_Error_Size, 2, YENV, P)) {
	Yap_Error(OUT_OF_STACK_ERROR, TermNil, Yap_ErrorMessage);
	return FALSE;
      }
    }
    /* release space for cur_instance */
    keepdbrefs(cur_instance->DBT);
    ErasePendingRefs(cur_instance->DBT);
    FreeDBSpace((char *) cur_instance->DBT);
    FreeDBSpace((char *) cur_instance);
    return(Yap_unify(ARG2, TDB));
  }
}

static Int
p_clean_queues(void)
{
  return(TRUE);
}

/* set the logical updates flag */
static Int
p_slu(void)
{
  Term t = Deref(ARG1);
  if (IsVarTerm(t)) { 
    Yap_Error(INSTANTIATION_ERROR, t, "switch_logical_updates/1");
    return(FALSE);
  } 
  if (!IsIntTerm(t)) { 
    Yap_Error(TYPE_ERROR_INTEGER, t, "switch_logical_updates/1");
    return(FALSE);
  }
  UPDATE_MODE = IntOfTerm(t);
  return(TRUE);
}

/* check current status for logical updates */
static Int
p_lu(void)
{
  return(Yap_unify(ARG1,MkIntTerm(UPDATE_MODE)));
}

/* get a hold over the index table for logical update predicates */ 
static Int
p_hold_index(void)
{
  Yap_Error(SYSTEM_ERROR, TermNil, "hold_index in debugger");
  return FALSE;
}

static Int
p_fetch_reference_from_index(void)
{
  Term t1 = Deref(ARG1), t2 = Deref(ARG2);
  DBRef table, el;
  Int pos;

  if (IsVarTerm(t1) || !IsDBRefTerm(t1))
    return(FALSE);
  table = DBRefOfTerm(t1);

  if (IsVarTerm(t2) || !IsIntTerm(t2))
    return(FALSE);
  pos = IntOfTerm(t2);
  el = (DBRef)(table->DBT.Contents[pos]);
#if defined(YAPOR) || defined(THREADS)
  LOCK(el->lock);
  TRAIL_REF(el);	/* So that fail will erase it */
  INC_DBREF_COUNT(el);
  UNLOCK(el->lock);
#else
  if (!(el->Flags & InUseMask)) {
    el->Flags |= InUseMask;
    TRAIL_REF(el);
  }
#endif
  return(Yap_unify(ARG3, MkDBRefTerm(el)));
}

static Int
p_resize_int_keys(void)
{
  Term t1 = Deref(ARG1);
  if (IsVarTerm(t1)) {
    return(Yap_unify(ARG1,MkIntegerTerm((Int)INT_KEYS_SIZE)));
  }
  if (!IsIntegerTerm(t1)) {
    Yap_Error(TYPE_ERROR_INTEGER, t1, "yap_flag(resize_db_int_keys,T)");
    return(FALSE);
  }
  return(resize_int_keys(IntegerOfTerm(t1)));
}

static void 
ReleaseTermFromDB(DBTerm *ref)
{
  keepdbrefs(ref);
  FreeDBSpace((char *)ref);
}

void 
Yap_ReleaseTermFromDB(DBTerm *ref)
{
  ReleaseTermFromDB(ref);
}

static Int 
p_install_thread_local(void)
{				/* '$is_dynamic'(+P)	 */
#if THREADS
  PredEntry      *pe;
  Term            t = Deref(ARG1);
  Term            mod = Deref(ARG2);

  if (IsVarTerm(t)) {
    return (FALSE);
  }
  if (mod == IDB_MODULE) {
    pe = find_lu_entry(t); 
    if (!pe->cs.p_code.NOfClauses) {
      if (IsIntegerTerm(t)) 
	pe->PredFlags |= LogUpdatePredFlag|NumberDBPredFlag;
      else if (IsAtomTerm(t))
	pe->PredFlags |= LogUpdatePredFlag|AtomDBPredFlag;
      else
	pe->PredFlags |= LogUpdatePredFlag;
    }
  } else if (IsAtomTerm(t)) {
    Atom at = AtomOfTerm(t);
    pe = RepPredProp(PredPropByAtom(at, mod));
  } else if (IsApplTerm(t)) {
    Functor         fun = FunctorOfTerm(t);
    pe = RepPredProp(PredPropByFunc(fun, mod));
  } else {
    return FALSE;
  }
  WRITE_LOCK(pe->PRWLock);
  if (pe->PredFlags & (UserCPredFlag|HiddenPredFlag|CArgsPredFlag|SyncPredFlag|TestPredFlag|AsmPredFlag|StandardPredFlag|CPredFlag|SafePredFlag|IndexedPredFlag|BinaryTestPredFlag) ||
      pe->cs.p_code.NOfClauses) {
    return FALSE;
  }
  pe->PredFlags |= ThreadLocalPredFlag;
  pe->OpcodeOfPred = Yap_opcode(_thread_local);
  pe->CodeOfPred = (yamop *)&pe->OpcodeOfPred;
  WRITE_UNLOCK(pe->PRWLock);
#endif
  return TRUE;
}

void 
Yap_InitDBPreds(void)
{
  Yap_InitCPred("recorded", 3, p_recorded, SyncPredFlag);
  Yap_InitCPred("recorda", 3, p_rcda, SyncPredFlag);
  Yap_InitCPred("recordz", 3, p_rcdz, SyncPredFlag);
  Yap_InitCPred("$still_variant", 2, p_still_variant, SyncPredFlag);
  Yap_InitCPred("recorda_at", 3, p_rcda_at, SyncPredFlag);
  Yap_InitCPred("recordz_at", 3, p_rcdz_at, SyncPredFlag);
  Yap_InitCPred("$recordap", 3, p_rcdap, SyncPredFlag);
  Yap_InitCPred("$recordzp", 3, p_rcdzp, SyncPredFlag);
  Yap_InitCPred("$recordap", 4, p_drcdap, SyncPredFlag);
  Yap_InitCPred("$recordzp", 4, p_drcdzp, SyncPredFlag);
  Yap_InitCPred("erase", 1, p_erase, SafePredFlag|SyncPredFlag);
  Yap_InitCPred("$erase_clause", 2, p_erase_clause, SafePredFlag|SyncPredFlag);
  Yap_InitCPred("erased", 1, p_erased, TestPredFlag | SafePredFlag|SyncPredFlag);
  Yap_InitCPred("instance", 2, p_instance, SyncPredFlag);
  Yap_InitCPred("$instance_module", 2, p_instance_module, SyncPredFlag);
  Yap_InitCPred("eraseall", 1, p_eraseall, SafePredFlag|SyncPredFlag);
  Yap_InitCPred("$record_stat_source", 4, p_rcdstatp, SafePredFlag|SyncPredFlag);
  Yap_InitCPred("$some_recordedp", 1, p_somercdedp, SafePredFlag|SyncPredFlag);
  Yap_InitCPred("$first_instance", 3, p_first_instance, SafePredFlag|SyncPredFlag);
  Yap_InitCPred("$init_db_queue", 1, p_init_queue, SafePredFlag|SyncPredFlag);
  Yap_InitCPred("$db_key", 2, p_db_key, 0);
  Yap_InitCPred("$db_enqueue", 2, p_enqueue, SyncPredFlag);
  Yap_InitCPred("$db_dequeue", 2, p_dequeue, SyncPredFlag);
  Yap_InitCPred("$db_clean_queues", 1, p_clean_queues, SyncPredFlag);
  Yap_InitCPred("$switch_log_upd", 1, p_slu, SafePredFlag|SyncPredFlag);
  Yap_InitCPred("$log_upd", 1, p_lu, SafePredFlag|SyncPredFlag);
  Yap_InitCPred("$hold_index", 3, p_hold_index, SafePredFlag|SyncPredFlag);
  Yap_InitCPred("$fetch_reference_from_index", 3, p_fetch_reference_from_index, SafePredFlag|SyncPredFlag);
  Yap_InitCPred("$resize_int_keys", 1, p_resize_int_keys, SafePredFlag|SyncPredFlag);
  Yap_InitCPred("key_statistics", 4, p_key_statistics, SyncPredFlag);
#ifdef DEBUG
  Yap_InitCPred("total_erased", 4, p_total_erased, SyncPredFlag);
  Yap_InitCPred("key_erased_statistics", 5, p_key_erased_statistics, SyncPredFlag);
  Yap_InitCPred("heap_space_info", 2, p_heap_space_info, SyncPredFlag);
#endif
  Yap_InitCPred("nth_instance", 3, p_nth_instance, SyncPredFlag);
  Yap_InitCPred("$nth_instancep", 3, p_nth_instancep, SyncPredFlag);
  Yap_InitCPred("$jump_to_next_dynamic_clause", 0, p_jump_to_next_dynamic_clause, SyncPredFlag);
  Yap_InitCPred("$install_thread_local", 2, p_install_thread_local, SafePredFlag);
}

void 
Yap_InitBackDB(void)
{
  Yap_InitCPredBack("$recorded_with_key", 3, 3, in_rded_with_key, co_rded, SyncPredFlag);
  RETRY_C_RECORDED_K_CODE = NEXTOP(PredRecordedWithKey->cs.p_code.FirstClause,lds);
  Yap_InitCPredBack("$recordedp", 3, 3, in_rdedp, co_rdedp, SyncPredFlag);
  RETRY_C_RECORDEDP_CODE = NEXTOP(RepPredProp(PredPropByFunc(Yap_MkFunctor(Yap_LookupAtom("$recordedp"), 3),0))->cs.p_code.FirstClause,lds);
  Yap_InitCPredBack("$current_immediate_key", 2, 4, init_current_key, cont_current_key,
		SyncPredFlag);
}

