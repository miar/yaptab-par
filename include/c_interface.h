/*************************************************************************
*									 *
*	 YAP Prolog 	@(#)c_interface.h	2.2			 *
*	Yap Prolog was developed at NCCUP - Universidade do Porto	 *
*									 *
* Copyright L.Damas, V.S.Costa and Universidade do Porto 1985-1997	 *
*									 *
**************************************************************************
*									 *
* File:		c_interface.h						 *
* Last rev:	19/2/88							 *
* mods:									 *
* comments:	c_interface header file for YAP				 *
*									 *
*************************************************************************/

/*******************  IMPORTANT ********************
   Due to a limitation of the DecStation loader any function (including
   library functions) which is linked to yap can not be called directly
   from C code loaded dynamically.
      To go around this problem we adopted the solution of calling such
   functions indirectly
****************************************************/

#include "yap_structs.h"

#if defined(_MSC_VER) && defined(YAP_EXPORTS)
#define X_API __declspec(dllexport)
#else
#define X_API
#endif

/* Primitive Functions */

/*  Term Deref(Term)  */
extern X_API Term PROTO(YapA,(int));
#ifdef IndirectCalls
static Term (*YapIA)() = YapA;
#define A(I) (*YapIA)(I)
#else
#define A(I) YapA(I)
#endif
#define ARG1	A(1)
#define ARG2	A(2)
#define ARG3	A(3)
#define ARG4	A(4)
#define ARG5	A(5)
#define ARG6	A(6)
#define ARG7	A(7)
#define ARG8	A(8)
#define ARG9	A(9)
#define ARG10	A(10)
#define ARG11	A(11)
#define ARG12	A(12)
#define ARG13	A(13)
#define ARG14	A(14)
#define ARG15	A(15)
#define ARG16	A(16)

/*  Term Deref(Term)  */
extern X_API Term PROTO(Deref,(Term));
#ifdef IndirectCalls
static Term (*YapIDeref)() = Deref;
#define Deref(T) (*YapIDeref)(T)
#endif

/*  Bool IsVarTerm(Term) */
extern X_API Bool PROTO(YapIsVarTerm,(Term));
#ifdef IndirectCalls
static Bool (*YapIIsVarTerm)() = YapIsVarTerm;
#define IsVarTerm(T) (*YapIIsVarTerm)(T)
#else
#define IsVarTerm(T) YapIsVarTerm(T)
#endif

/*  Bool IsNonVarTerm(Term) */
extern X_API Bool PROTO(YapIsNonVarTerm,(Term));
#ifdef IndirectCalls
static Bool (*YapIIsNonVarTerm)() = YapIsNonVarTerm;
#define IsNonVarTerm(T) (*YapIIsNonVarTerm)(T)
#else
#define IsNonVarTerm(T) YapIsNonVarTerm(T)
#endif

/*  Term  MkVarTerm()  */
extern X_API Term PROTO(YapMkVarTerm,(void));
#ifdef IndirectCalls
static Term (*YapIMkVarTerm)() = YapMkVarTerm;
#define MkVarTerm() (*YapIMkVarTerm)()
#else
#define MkVarTerm() YapMkVarTerm()
#endif

/*  Bool IsIntTerm(Term)  */
extern X_API Bool PROTO(YapIsIntTerm,(Term));
#ifdef IndirectCalls
static Bool (*YapIIsIntTerm)() = YapIsIntTerm;
#define IsIntTerm(T) (*YapIIsIntTerm)(T)
#else
#define IsIntTerm(T) YapIsIntTerm(T)
#endif

/*  Bool IsFloatTerm(Term)  */
extern X_API Bool PROTO(YapIsFloatTerm,(Term));
#ifdef IndirectCalls
static Bool (*YapIIsFloatTerm)() = YapIsFloatTerm;
#define IsFloatTerm(T) (*YapIIsFloatTerm)(T)
#else
#define IsFloatTerm(T) YapIsFloatTerm(T)
#endif

/*  Bool IsDbRefTerm(Term)  */
extern X_API Bool PROTO(YapIsDbRefTerm,(Term));
#ifdef IndirectCalls
static Bool (*YapIIsDbRefTerm)() = YapIsDbRefTerm;
#define IsDbRefTerm(T) (*YapIIsDbRefTerm)(T)
#else
#define IsDbRefTerm(T) YapIsDbRefTerm(T)
#endif

/*  Bool IsAtomTerm(Term)  */
extern X_API Bool PROTO(YapIsAtomTerm,(Term));
#ifdef IndirectCalls
static Bool (*YapIIsAtomTerm)() = YapIsAtomTerm;
#define IsAtomTerm(T) (*YapIIsAtomTerm)(T)
#else
#define IsAtomTerm(T) YapIsAtomTerm(T)
#endif

/*  Bool IsPairTerm(Term)  */
extern X_API Bool PROTO(YapIsPairTerm,(Term));
#ifdef IndirectCalls
static Bool (*YapIIsPairTerm)() = YapIsPairTerm;
#define IsPairTerm(T) (*YapIIsPairTerm)(T)
#else
#define IsPairTerm(T) YapIsPairTerm(T)
#endif

/*  Bool IsApplTerm(Term)  */
extern X_API Bool PROTO(YapIsApplTerm,(Term));
#ifdef IndirectCalls
static Bool (*YapIIsApplTerm)() = YapIsApplTerm;
#define IsApplTerm(T) (*YapIIsApplTerm)(T)
#else
#define IsApplTerm(T) YapIsApplTerm(T)
#endif

/*    Term MkIntTerm(Int)  */
extern X_API Term PROTO(YapMkIntTerm,(Int));
#ifdef IndirectCalls
static Term (*YapIMkIntTerm)() = YapMkIntTerm;
#define MkIntTerm(T) (*YapIMkIntTerm)(T)
#else
#define MkIntTerm(T) YapMkIntTerm(T)
#endif

/*    Int  IntOfTerm(Term) */
extern X_API Int PROTO(YapIntOfTerm,(Term));
#ifdef IndirectCalls
static Int (*YapIIntOfTerm)() = YapIntOfTerm;
#define IntOfTerm(T) (*YapIIntOfTerm)(T)
#else
#define IntOfTerm(T) YapIntOfTerm(T)
#endif

/*    Term MkFloatTerm(flt)  */
extern X_API Term PROTO(YapMkFloatTerm,(flt));
#ifdef IndirectCalls
static Term (*YapIMkFloatTerm)() = YapMkFloatTerm;
#define MkFloatTerm(T) (*YapIMkFloatTerm)(T)
#else
#define MkFloatTerm(T) YapMkFloatTerm(T)
#endif

/*    flt  FloatOfTerm(Term) */
extern X_API flt PROTO(YapFloatOfTerm,(Term));
#ifdef IndirectCalls
static flt (*YapIFloatOfTerm)() = YapFloatOfTerm;
#define FloatOfTerm(T) (*YapIFloatOfTerm)(T)
#else
#define FloatOfTerm(T) YapFloatOfTerm(T)
#endif

/*    Term MkAtomTerm(Atom)  */
extern X_API Term PROTO(YapMkAtomTerm,(Atom));
#ifdef IndirectCalls
static Term (*YapIMkAtomTerm)() = YapMkAtomTerm;
#define MkAtomTerm(T) (*YapIMkAtomTerm)(T)
#else
#define MkAtomTerm(T) YapMkAtomTerm(T)
#endif

/*    Atom  AtomOfTerm(Term) */
extern X_API Atom PROTO(YapAtomOfTerm,(Term));
#ifdef IndirectCalls
static Atom (*YapIAtomOfTerm)() = YapAtomOfTerm;
#define AtomOfTerm(T) (*YapIAtomOfTerm)(T)
#else
#define AtomOfTerm(T) YapAtomOfTerm(T)
#endif

/*    Atom  LookupAtom(char *) */
extern X_API Atom PROTO(YapLookupAtom,(char *));
#ifdef IndirectCalls
static Atom (*YapILookupAtom)() = YapLookupAtom;
#define LookupAtom(T) (*YapILookupAtom)(T)
#else
#define LookupAtom(T) YapLookupAtom(T)
#endif

/*    Atom  FullLookupAtom(char *) */
extern X_API Atom PROTO(YapFullLookupAtom,(char *));
#ifdef IndirectCalls
static Atom (*YapIFullLookupAtom)() = YapFullLookupAtom;
#define FullLookupAtom(T) (*YapIFullLookupAtom)(T)
#else
#define FullLookupAtom(T) YapFullLookupAtom(T)
#endif

/*    char* AtomName(Atom) */
extern X_API char *PROTO(YapAtomName,(Atom));
#ifdef IndirectCalls
static char *((*YapIAtomName)()) = YapAtomName;
#define AtomName(T) (*YapIAtomName)(T)
#else
#define AtomName(T) YapAtomName(T)
#endif

/*    Term  MkPairTerm(Term Head, Term Tail) */
extern X_API Term PROTO(YapMkPairTerm,(Term,Term));
#ifdef IndirectCalls
static Term (*YapIMkPairTerm)() = YapMkPairTerm;
#define MkPairTerm(T1,T2) (*YapIMkPairTerm)(T1,T2)
#else
#define MkPairTerm(T1,T2) YapMkPairTerm(T1,T2)
#endif

/*    Term  MkNewPairTerm(void) */
extern X_API Term PROTO(YapMkNewPairTerm,(void));
#ifdef IndirectCalls
static Term (*YapIMkNewPairTerm)() = YapMkNewPairTerm;
#define MkNewPairTerm() (*YapIMkNewPairTerm)()
#else
#define MkNewPairTerm() YapMkNewPairTerm()
#endif

/*    Term  HeadOfTerm(Term)  */
extern X_API Term PROTO(YapHeadOfTerm,(Term));
#ifdef IndirectCalls
static Term (*YapIHeadOfTerm)() = YapHeadOfTerm;
#define HeadOfTerm(T) (*YapIHeadOfTerm)(T)
#else
#define HeadOfTerm(T) YapHeadOfTerm(T)
#endif

/*    Term  TailOfTerm(Term)  */
extern X_API Term PROTO(YapTailOfTerm,(Term));
#ifdef IndirectCalls
static Term (*YapITailOfTerm)() = YapTailOfTerm;
#define TailOfTerm(T) (*YapITailOfTerm)(T)
#else
#define TailOfTerm(T) YapTailOfTerm(T)
#endif


/*    Term     MkApplTerm(Functor f, int n, Term[] args) */
extern X_API Term PROTO(YapMkApplTerm,(Functor,int,Term *));
#ifdef IndirectCalls
static Term (*YapIMkApplTerm)() = YapMkApplTerm;
#define MkApplTerm(F,N,As) (*YapIMkApplTerm)(F,N,As)
#else
#define MkApplTerm(F,N,As) YapMkApplTerm(F,N,As)
#endif

/*    Term     MkNewApplTerm(Functor f, int n) */
extern X_API Term PROTO(YapMkNewApplTerm,(Functor,int));
#ifdef IndirectCalls
static Term (*YapIMkNewApplTerm)() = YapMkNewApplTerm;
#define MkNewApplTerm(F,N) (*YapIMkNewApplTerm)(F,N)
#else
#define MkNewApplTerm(F,N) YapMkNewApplTerm(F,N)
#endif


/*    Functor  FunctorOfTerm(Term)  */
extern X_API Functor PROTO(YapFunctorOfTerm,(Term));
#ifdef IndirectCalls
static Functor (*YapIFunctorOfTerm)() = YapFunctorOfTerm;
#define FunctorOfTerm(T) (*YapIFunctorOfTerm)(T)
#else
#define FunctorOfTerm(T) YapFunctorOfTerm(T)
#endif

/*    Term     ArgOfTerm(int argno,Term t) */
extern X_API Term PROTO(YapArgOfTerm,(int,Term));
#ifdef IndirectCalls
static Term (*YapIArgOfTerm)() = YapArgOfTerm;
#define ArgOfTerm(N,T) (*YapIArgOfTerm)(N,T)
#else
#define ArgOfTerm(N,T) YapArgOfTerm(N,T)
#endif

/*    Functor  MkFunctor(Atom a,int arity) */
extern X_API Functor PROTO(YapMkFunctor,(Atom,int));
#ifdef IndirectCalls
static Functor (*YapIMkFunctor)() = YapMkFunctor;
#define MkFunctor(A,N) (*YapIMkFunctor)(A,N)
#else
#define MkFunctor(A,N) YapMkFunctor(A,N)
#endif

/*    Atom     NameOfFunctor(Functor) */
extern X_API Atom PROTO(YapNameOfFunctor,(Functor));
#ifdef IndirectCalls
static Atom (*YapINameOfFunctor)() = YapNameOfFunctor;
#define NameOfFunctor(T) (*YapINameOfFunctor)(T)
#else
#define NameOfFunctor(T) YapNameOfFunctor(T)
#endif

/*    Int     ArityOfFunctor(Functor) */
extern X_API Int PROTO(YapArityOfFunctor,(Functor));
#ifdef IndirectCalls
static Int (*YapIArityOfFunctor)() = YapArityOfFunctor;
#define ArityOfFunctor(T) (*YapIArityOfFunctor)(T)
#else
#define ArityOfFunctor(T) YapArityOfFunctor(T)
#endif

/*  void ExtraSpace(void) */
extern X_API void *PROTO(YapExtraSpace,(void));
#ifdef IndirectCalls
static void *(*YapIExtraSpace)() = YapExtraSpace;
#define YapExtraSpace() (*YapExtraSpace)()
#endif

#define  PRESERVE_DATA(ptr, type) (ptr = (type *)YapExtraSpace())
#define PRESERVED_DATA(ptr, type) (ptr = (type *)YapExtraSpace())

/*   Int      unify(Term a, Term b) */
extern X_API Int PROTO(YapUnify,(Term, Term));
#ifdef IndirectCalls
static Int (*YapIUnify)() = YapUnify;
#define unify(T1,T2) (*YapIUnify)(T1,T2)
#else
#define unify(T1,T2) YapUnify(T1,T2)
#endif

/*  void UserCPredicate(char *name, int *fn(), int arity) */
extern X_API void PROTO(UserCPredicate,(char *, int (*)(void), int));
#ifdef IndirectCalls
static void (*YapIUserCPredicate)() = UserCPredicate;
#define UserCPredicate(N,F,A) (*YapIUserCPredicate)(N,F,A)
#endif

/*  void UserBackCPredicate(char *name, int *init(), int *cont(), int
    arity, int extra) */
extern X_API void PROTO(UserBackCPredicate,(char *, int (*)(void), int (*)(void), int, int));
#ifdef IndirectCalls
static void (*YapIUserBackCPredicate)() = UserBackCPredicate;
#define UserBackCPredicate(N,F,G,A,B) (*YapIUserBackCPredicate)(N,F,G,A,B)
#endif

/*  void UserCPredicate(char *name, int *fn(), int arity) */
extern X_API void PROTO(YapUserCPredicateWithArgs,(char *, int (*)(void), Int,Int));
#ifdef IndirectCalls
static void (*YapIUserCPredicateWithArgs)() = UserCPredicateWithArgs;
#define YapUserCPredicateWithArgs(N,F,A,M) (*YapIUserCPredicateWithArgs)(N,F,A,M)
#endif

/*  void CallProlog(Term t) */
extern X_API Int PROTO(YapCallProlog,(Term t));
#ifdef IndirectCalls
static Int (*YapICallProlog)() = YapCallProlog;
#define CallProlog(t) (*YapICallProlog)(t)
#else
#define CallProlog(t) YapCallProlog(t)
#endif

/*  void cut_fail(void) */
extern X_API Int PROTO(Yapcut_fail,(void));
#ifdef IndirectCalls
static Int (*YapIcut_fail)() = Yapcut_fail;
#define cut_fail() (*YapIcut_fail)()
#else
#define cut_fail() Yapcut_fail()
#endif

/*  void cut_succeed(void) */
extern X_API Int PROTO(Yapcut_succeed,(void));
#ifdef IndirectCalls
static Int (*YapIcut_succeed)() = Yapcut_succeed;
#define cut_succeed() (*YapIcut_succeed)()
#else
#define cut_succeed() Yapcut_succeed()
#endif

/*  void *AllocSpaceFromYap(int) */
extern X_API void *PROTO(YapAllocSpaceFromYap,(unsigned int));
#ifdef IndirectCalls
static void (*YapIAllocSpaceFromYap)() = YapAllocSpaceFromYap;
#define AllocSpaceFromYap(SIZE) (*YapIAllocSpaceFromYap)(SIZE)
#else
#define AllocSpaceFromYap(SIZE) YapAllocSpaceFromYap(SIZE)
#endif

/*  void FreeSpaceFromYap(void *) */
extern X_API void PROTO(YapFreeSpaceFromYap,(void *));
#ifdef IndirectCalls
static void (YapIFreeSpaceFromYap)() = YapFreeSpaceFromYap;
#define FreeSpaceFromYap(PTR) (*YapIFreeSpaceFromYap)(PTR)
#else
#define FreeSpaceFromYap(PTR) YapFreeSpaceFromYap(PTR)
#endif

/*  int YapRunGoal(Term) */
extern X_API int PROTO(YapRunGoal,(Term));
#ifdef IndirectCalls
static int (YapIRunGoal)() = YapRunGoal;
#define YapRunGoal(T) (*YapIRunGoal)(T)
#endif

/*  int YapRestartGoal(void) */
extern X_API int PROTO(YapRestartGoal,(void));
#ifdef IndirectCalls
static int (YapIRestartGoal)() = YapRestartGoal;
#define YapRestartGoal() (*YapIRestartGoal)()
#endif

/*  int YapContinueGoal(void) */
extern X_API int PROTO(YapContinueGoal,(void));
#ifdef IndirectCalls
static int (YapIContinueGoal)() = YapContinueGoal;
#define YapContinueGoal() (*YapIContinueGoal)()
#endif

/*  void YapPruneGoal(void) */
extern X_API void PROTO(YapPruneGoal,(void));
#ifdef IndirectCalls
static void (YapIPruneGoal)() = YapPruneGoal;
#define YapPruneGoal() (*YapIPruneGoal)()
#endif

/*  int YapGoalHasException(void) */
extern X_API int PROTO(YapGoalHasException,(Term *));
#ifdef IndirectCalls
static int (YapIGoalHasException)(TP) = YapGoalHasException;
#define YapGoalHasException(TP) (*YapIGoalHasException)(TP)
#endif

/*  int YapReset(void) */
extern X_API void PROTO(YapReset,(void));
#ifdef IndirectCalls
static void (YapIReset)() = YapReset;
#define YapReset() (*YapIReset)()
#endif

/*  void YapError(char *) */
extern X_API void PROTO(YapError,(char *));
#ifdef IndirectCalls
static void (YapIError)() = YapError;
#define YapError(T) (*YapIError)(T)
#endif

/*  Term YapRead(int (*)(void)) */
extern X_API Term PROTO(YapRead,(int (*)(void)));
#ifdef IndirectCalls
static Term (YapIRead)() = YapRead;
#define YapRead(F) (*YapIRead)(F)
#endif

/*  void YapWrite(Term,void (*)(int),int) */
extern X_API void PROTO(YapWrite,(Term,void (*)(int),int));
#ifdef IndirectCalls
static void (YapIWrite)() = YapWrite;
#define YapWrite(T,W,F) (*YapIWrite)(T,W,F)
#endif

/*  char *YapCompileClause(Term) */
extern X_API char *PROTO(YapCompileClause,(Term));
#ifdef IndirectCalls
static char *(YapICompileClause)() = YapCompileClause;
#define YapCompileClause(C) (*YapICompileClause)(C)
#endif

/*  int YapInit(yap_init_args *) */
extern X_API int PROTO(YapInit,(yap_init_args *));
#ifdef IndirectCalls
static int (YapIInit)() = YapInit;
#define YapInit(T) (*YapIInit)(T)
#endif

/*  int YapFastInit(char *) */
extern X_API int PROTO(YapFastInit,(char *));
#ifdef IndirectCalls
static int (YapIFastInit)() = YapFastInit;
#define YapFastInit(S) (*YapIFastInit)(S)
#endif

/*  int YapInitConsult(int, char *) */
extern X_API int PROTO(YapInitConsult,(int, char *));
#ifdef IndirectCalls
static int (YapIInitConsult)() = YapInitConsult;
#define YapInitConsult(M,F) (*YapIInitConsult)(M,F)
#endif

/*  int YapStartConsult(int, char *) */
extern X_API int PROTO(YapEndConsult,(void));
#ifdef IndirectCalls
static int (YapIEndConsult)() = YapEndConsult;
#define YapEndConsult(M,F) (*YapIEndConsult)(M,F)
#endif

/*  void YapExit(int) */
extern X_API void PROTO(YapExit,(int));
#ifdef IndirectCalls
static int (YapIExit)() = YapExit;
#define YapExit(I) (*YapIExit)(I)
#endif

/*  void YapPutValue(Atom, Term) */
extern X_API void PROTO(YapPutValue,(Atom, Term));
#ifdef IndirectCalls
static Term (YapIPutValue)() = YapPutValue;
#define YapPutValue(A,T) (*YapIPutValue)(A,T)
#endif

/*  Term YapGetValue(Atom) */
extern X_API Term PROTO(YapGetValue,(Atom));
#ifdef IndirectCalls
static Term (YapIGetValue)() = YapGetValue;
#define YapGetValue(A) (*YapIGetValue)(A)
#endif

/*  int StringToBuffer(Term,char *,unsigned int) */
extern X_API int PROTO(YapStringToBuffer,(Term,char *,unsigned int));
#ifdef IndirectCalls
static void (YapIStringToBuffer)() = YapStringToBuffer;
#define StringToBuffer(T,BUF,SIZE) (*YapIStringToBuffer)(T,BUF,SIZE)
#else
#define StringToBuffer(T,BUF,SIZE) YapStringToBuffer(T,BUF,SIZE)
#endif

/*  int BufferToString(char *) */
extern X_API Term PROTO(YapBufferToString,(char *));
#ifdef IndirectCalls
static void (YapIBufferToString)() = YapBufferToString;
#define BufferToString(BUF) (*YapIBufferToString)(BUF)
#else
#define BufferToString(BUF) YapBufferToString(BUF)
#endif

/*  int BufferToAtomList(char *) */
extern X_API Term PROTO(YapBufferToAtomList,(char *));
#ifdef IndirectCalls
static void (YapIBufferToAtomList)() = YapBufferToAtomList;
#define BufferToAtomList(BUF) (*YapIBufferToAtomList)(BUF)
#else
#define BufferToAtomList(BUF) YapBufferToAtomList(BUF)
#endif

/*  void YapInitSocks(char *,long) */
extern X_API int PROTO(YapInitSocks,(char *,long));
#ifdef IndirectCalls
static int (YapIInitSocks)(char *,long) = YapInitSocks;
#define YapInitSocks(S,I) (*YapIInitSocks)(S,I)
#endif

#ifdef  SFUNC

#define SFArity  0
extern X_API Term *ArgsOfSFTerm();
#ifdef IndirectCalls
static Term *((*YapIArgsOfSFTerm)()) = ArgsOfSFTerm;
#define ArgsOfSFTerm(T) (*YapIArgsOfSFTerm)(T)
#endif
extern X_API Term MkSFTerm();
#ifdef IndirectCalls
static Term (*YapIMkSFTerm)() = MkSFTerm;
#define MkSFTerm(F,N,A,EV) (*YapIMkSFTerm)(F,N,A,EV)
#endif

#endif /* SFUNC */

/*  Term  YapSetOutputMessage()  */
extern X_API void PROTO(YapSetOutputMessage,(void));
#ifdef IndirectCalls
static void (*YapISetOutputMessage)() = YapSetOutputMessage;
#define YapSetOutputMessage() (*YapISetOutputMessage)()
#endif

/*  Term  YapSetOutputMessage()  */
extern X_API int PROTO(YapStreamToFileNo,(Term));
#ifdef IndirectCalls
static void (*YapIStreamToFileNo)() = YapStreamToFileNo;
#define YapStreamToFileNo() (*YapIStreamToFileNo)()
#endif

/*  Term  YapSetOutputMessage()  */
extern X_API void PROTO(YapCloseAllOpenStreams,(void));
#ifdef IndirectCalls
static void (*YapICloseAllOpenStreams)() = YapCloseAllOpenStreams;
#define YapCloseAllOpenStreams() (*YapICloseAllOpenStreams)()
#endif

#define YAP_INPUT_STREAM	0x01
#define YAP_OUTPUT_STREAM	0x02
#define YAP_APPEND_STREAM	0x04
#define YAP_PIPE_STREAM 	0x08
#define YAP_TTY_STREAM	 	0x10
#define YAP_POPEN_STREAM	0x20
#define YAP_BINARY_STREAM	0x40
#define YAP_SEEKABLE_STREAM	0x80

/*  Term  YapOpenStream()  */
extern X_API Term PROTO(YapOpenStream,(void *, char *, Term, int));
#ifdef IndirectCalls
static Term (*YapIOpenStream)() = YapOpenStream;
#define YapOpenStream(FD,S,T,FL) (*YapIOpenStream)(FD,S,T,FL)
#endif

/*  Term  *YapNewSlots()  */
extern X_API long PROTO(YapNewSlots,(int));
#ifdef IndirectCalls
static long (*YapINewSlots)(N) = YapNewSlots;
#define YapNewSlots(N) (*YapINewSlots)(N)
#endif

/*  Term  *YapInitSlot()  */
extern X_API long PROTO(YapInitSlot,(Term));
#ifdef IndirectCalls
static long (*YapIInitSlot)(T) = YapInitSlot;
#define YapInitSlot(T) (*YapIInitSlot)(T)
#endif

/*  Term  YapGetFromSlots(t)  */
extern X_API Term PROTO(YapGetFromSlot,(long));
#ifdef IndirectCalls
static Term (*YapIGetFromSlot)(N) = YapGetFromSlot;
#define YapGetFromSlot(N) (*YapIGetFromSlot)(N)
#endif

/*  Term  YapAddressFromSlots(t)  */
extern X_API Term *PROTO(YapAddressFromSlot,(long));
#ifdef IndirectCalls
static Term *(*YapIAddressFromSlot)(N) = YapAddressFromSlot;
#define YapAddressFromSlot(N) (*YapIAddressFromSlot)(N)
#endif

/*  Term  YapPutInSlots(t)  */
extern X_API void PROTO(YapPutInSlot,(long, Term));
#ifdef IndirectCalls
static void (*YapIPutInSlot)(N,T) = YapPutInSlot;
#define YapPutInSlot(N,T) (*YapIPutInSlot)(N,T)
#endif

/*  void  YapRecoverSlots()  */
extern X_API void PROTO(YapRecoverSlots,(int));
#ifdef IndirectCalls
static void (*YapIRecoverSlots)(N) = YapRecoverSlots;
#define YapRecoverSlots(N) (*YapIRecoverSlots)(N)
#endif

/*  void  YapThrow()  */
extern X_API void PROTO(YapThrow,(Term));
#ifdef IndirectCalls
static void (*YapIThrow)(T) = YapThrow;
#define YapThrow(T) (*YapIThrow)(T)
#endif

/*  int  YapLookupModule()  */
extern X_API int  PROTO(YapLookupModule,(Term));
#ifdef IndirectCalls
static int (*YapILookupModule)(T) = YapLookupModule;
#define YapLookupModule(T) (*YapILookupModule)(T)
#endif

/*  int  YapModuleName()  */
extern X_API Term  PROTO(YapModuleName,(int));
#ifdef IndirectCalls
static int (*YapIModuleName)(I) = YapModuleName;
#define YapModuleName(I) (*YapIModuleName)(I)
#endif

/*  int  YapHalt()  */
extern X_API int  PROTO(YapHalt,(int));
#ifdef IndirectCalls
static int (*YapIHalt)(E) = YapHalt;
#define YapHalt(E) (*YapIHalt)(E)
#endif

/*  int  YapTopOfLocalStack()  */
extern X_API Term  *PROTO(YapTopOfLocalStack,(void));
#ifdef IndirectCalls
static Term *(*YapITopOfLocalStack)() = YapTopOfLocalStack;
#define YapTopOfLocalStack() (*YapITopOfLocalStack)()
#endif

/*  int  YapPredicate()  */
extern X_API void  *PROTO(YapPredicate,(Atom,Int,Int));
#ifdef IndirectCalls
static Term *(*YapIPredicate)(N,A,M) = YapPredicate;
#define YapPredicate(N,A,M) (*YapIPredicate)(N,A,M)
#endif

/*  int  YapPredicate()  */
extern X_API void  PROTO(YapPredicateInfo,(void *,Atom*,Int*,Int*));
#ifdef IndirectCalls
static void (*YapIPredicateInfo)(P,N,A,M) = YapPredicateInfo;
#define YapPredicateInfo(P,N,A,M) (*YapIPredicateInfo)(P,N,A,M)
#endif


/*  int  YapPredicate()  */
extern X_API int  PROTO(YapCurrentModule,(void));
#ifdef IndirectCalls
static int (*YapICurrentModule)() = YapCurrentModule;
#define YapCurrentModule() (*YapICurrentModule)()
#endif


#define InitCPred(N,A,F)  UserCPredicate(N,F,A)

