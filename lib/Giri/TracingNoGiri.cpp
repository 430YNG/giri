//===- TracingNoGiri.cpp - Dynamic Slicing Trace Instrumentation Pass -----===//
//
//                          Bug Diagnosis Compiler
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This files defines passes that are used for dynamic slicing.
//
// TODO:
//  Technically, we should support the tracing of signal handlers.  This can
//  interrupt the execution of a basic block.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "giri"

#include "Giri/Giri.h"
#include "Utility/Utils.h"
#include "Utility/VectorExtras.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Instructions.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

#include <vector>

//
// Command line arguments.
//
static cl::opt<std::string>
TraceFilename ("t", cl::desc("Trace filename"), cl::init("bbrecord"));

char giri::TracingNoGiri::ID = 0;

static RegisterPass<giri::TracingNoGiri>
X ("trace-giri", "Instrument code to trace basic block execution");

//
// Pass Statistics
//
namespace {
  STATISTIC (NumBBs, "Total number of basic blocks");
  STATISTIC (PHIBBs, "Total number of basic blocks with phi nodes");
  STATISTIC (Loads,  "Total number of load instructions processed");
  STATISTIC (Stores, "Total number of store instructions processed");
  STATISTIC (Selects, "Total number of select instructions processed");
  STATISTIC (LoadStrings,  "Total number of load instructions processed");
  STATISTIC (StoreStrings, "Total number of store instructions processed");
  STATISTIC (Calls,  "Total number of call instructions processed");
  STATISTIC (ExtFuns,  "Total number of special external calls like memcpy etc. processed");
}

//
// Function: hasPHI()
//
// Description:
//  This method determines whether the given basic block contains any PHI
//  instructions.
//
// Inputs:
//  BB - A reference to the Basic Block to analyze.  It is not modified.
//
// Return value:
//  true  - The basic block has one or more PHI instructions.
//  false - The basic block has no PHI instructions.
//
static bool hasPHI (const BasicBlock & BB) {
  for (BasicBlock::const_iterator I = BB.begin(); I != BB.end(); ++I)
    if (isa<PHINode>(I)) return true;
  return false;
}

//
// Method: doInitialialization()
//
// Description:
//  This method does module level changes needed for adding tracing
//  instrumentation for dynamic slicing.  Specifically, we add the function
//  prototypes for the dynamic slicing functionality here.
//
bool giri::TracingNoGiri::doInitialization (Module & M) {
  //
  // Get references to the different types that we'll need.
  //
  Int8Type  = IntegerType::getInt8Ty(M.getContext());
  Int32Type = IntegerType::getInt32Ty (M.getContext());
  Int64Type = IntegerType::getInt64Ty (M.getContext());
  VoidPtrType = PointerType::getUnqual(Int8Type);
  VoidType = Type::getVoidTy (M.getContext());

  initialize(M); // Initialize type variables for invariants

  //
  // Add the function for recording the execution of a basic block.
  //
  RecordBB = dyn_cast<Function>(M.getOrInsertFunction ("recordBB",
                                                       VoidType,
                                                       Int32Type,
                                                       VoidPtrType,
                                                       Int32Type,
                                                       NULL));

  //
  // Add the function for recording the start of execution of a basic block.
  //
  RecordStartBB = dyn_cast<Function>(M.getOrInsertFunction ("recordStartBB",
                                                            VoidType,
                                                            Int32Type,
                                                            VoidPtrType,
                                                            NULL));

  //
  // Add the functions for recording the execution of loads, stores, and calls.
  //
  RecordLoad = dyn_cast<Function>(M.getOrInsertFunction ("recordLoad",
                                                         VoidType,
                                                         Int32Type,
                                                         VoidPtrType,
                                                         Int64Type,
                                                         NULL));

  RecordStore = dyn_cast<Function>(M.getOrInsertFunction ("recordStore",
                                                          VoidType,
                                                          Int32Type,
                                                          VoidPtrType,
                                                          Int64Type,
                                                          NULL));

  RecordSelect = dyn_cast<Function>(M.getOrInsertFunction ("recordSelect",
                                                           VoidType,
                                                           Int32Type,
                                                           Int8Type,
                                                           NULL));

  RecordStrLoad = dyn_cast<Function>(M.getOrInsertFunction ("recordStrLoad",
                                                            VoidType,
                                                            Int32Type,
                                                            VoidPtrType,
                                                            NULL));

  RecordStrStore = dyn_cast<Function>(M.getOrInsertFunction ("recordStrStore",
                                                             VoidType,
                                                             Int32Type,
                                                             VoidPtrType,
                                                             NULL));

  RecordStrcatStore = dyn_cast<Function>(M.getOrInsertFunction ("recordStrcatStore",
                                                             VoidType,
                                                             Int32Type,
                                                             VoidPtrType,
                                                             VoidPtrType,
                                                             NULL));

  RecordCall = dyn_cast<Function>(M.getOrInsertFunction ("recordCall",
                                                          VoidType,
                                                          Int32Type,
                                                          VoidPtrType,
                                                          NULL));

  RecordReturn = dyn_cast<Function>(M.getOrInsertFunction ("recordReturn",
                                                          VoidType,
                                                          Int32Type,
                                                          VoidPtrType,
                                                          NULL));

  RecordExtCall = dyn_cast<Function>(M.getOrInsertFunction ("recordExtCall",
                                                          VoidType,
                                                          Int32Type,
                                                          VoidPtrType,
                                                          NULL));

  RecordExtCallRet = dyn_cast<Function>(M.getOrInsertFunction ("recordExtCallRet",
                                                          VoidType,
                                                          Int32Type,
                                                          VoidPtrType,
                                                          NULL));

  RecordExtFun = dyn_cast<Function>(M.getOrInsertFunction ("recordExtFun",
                                                          VoidType,
                                                          Int32Type,
                                                          Int32Type,
                                                          VoidPtrType,
                                                          VoidPtrType,
                                                          NULL));

  RecordHandlerThreadID=dyn_cast<Function>(M.getOrInsertFunction ("recordHandlerThreadID",
                                                 VoidType,
                                                 VoidPtrType,
                                                 NULL));

  //
  // Get a reference to the run-time's initialization function
  //
  Init=dyn_cast<Function>(M.getOrInsertFunction ("recordInit",
                                                 VoidType,
                                                 VoidPtrType,
                                                 NULL));

  return true;
}

//
// Function: createCtor()
//
// Description:
//  Create a global constructor (ctor) function that can be called when the
//  program starts up.
//
//
Function* giri::TracingNoGiri::createCtor (Module & M) {
  //
  // Create the ctor function.
  //
  Type * VoidTy = Type::getVoidTy (M.getContext());
  Function * RuntimeCtor = dyn_cast<Function>(M.getOrInsertFunction ("giriCtor", VoidTy, NULL));
  assert (RuntimeCtor && "Somehow created a non-function function!\n");

  //
  // Make the ctor function internal and non-throwing.
  //
  RuntimeCtor->setDoesNotThrow();
  RuntimeCtor->setLinkage(GlobalValue::InternalLinkage);

  //
  // Add a call in the new constructor function to the Giri initialization
  // function.
  //
  BasicBlock * BB = BasicBlock::Create (M.getContext(), "entry", RuntimeCtor);
  Constant * Name = stringToGV (TraceFilename, &M);
  Name = ConstantExpr::getZExtOrBitCast (Name, VoidPtrType);
  CallInst::Create (Init, Name, "", BB);

  //
  // Add a return instruction at the end of the basic block.
  //
  ReturnInst::Create (M.getContext(), BB);
  return RuntimeCtor;
}

//
// Method: insertIntoGlobalCtorList()
//
// Description:
//  Insert the specified function into the list of global constructor
//  functions.
//
void giri::TracingNoGiri::insertIntoGlobalCtorList (Function * RuntimeCtor) {
  //
  // Insert the run-time ctor into the ctor list.
  //
  LLVMContext & Context = RuntimeCtor->getParent()->getContext();
  Type * Int32Type = IntegerType::getInt32Ty (Context);
  std::vector<Constant *> CtorInits;
  CtorInits.push_back (ConstantInt::get (Int32Type, 65535));
  CtorInits.push_back (RuntimeCtor);
  Constant * RuntimeCtorInit=ConstantStruct::getAnon(Context,CtorInits, false);

  //
  // Get the current set of static global constructors and add the new ctor
  // to the list.
  //
  std::vector<Constant *> CurrentCtors;
  Module & M = *(RuntimeCtor->getParent());
  GlobalVariable * GVCtor = M.getNamedGlobal ("llvm.global_ctors");
  if (GVCtor) {
    if (Constant * C = GVCtor->getInitializer()) {
      for (unsigned index = 0; index < C->getNumOperands(); ++index) {
        CurrentCtors.push_back (cast<Constant>(C->getOperand (index)));
      }
    }

    //
    // Rename the global variable so that we can name our global
    // llvm.global_ctors.
    //
    GVCtor->setName ("removed");
  }

  //
  // The ctor list seems to be initialized in different orders on different
  // platforms, and the priority settings don't seem to work.  Examine the
  // module's platform string and take a best guess to the order.
  //
  if (M.getTargetTriple().find ("linux") == std::string::npos)
    CurrentCtors.insert (CurrentCtors.begin(), RuntimeCtorInit);
  else
    CurrentCtors.push_back (RuntimeCtorInit);

  //
  // Create a new initializer.
  //
  ArrayType * AT = ArrayType::get (RuntimeCtorInit-> getType(),
                                         CurrentCtors.size());
  Constant * NewInit=ConstantArray::get (AT, CurrentCtors);

  //
  // Create the new llvm.global_ctors global variable and replace all uses of
  // the old global variable with the new one.
  //
  new GlobalVariable (M,
                      NewInit->getType(),
                      false,
                      GlobalValue::AppendingLinkage,
                      NewInit,
                      "llvm.global_ctors");
}

//
// Method: doFinalization()
//
// Description:
//  This method is called after all the basic blocks have been transformed.  It
//  inserts code to initialize the run-time of the tracing library.
//
bool giri::TracingNoGiri::doFinalization (Module & M) {
  //
  // Create a global constructor function that will initialize the run-time.
  //
  Function * RuntimeCtor = createCtor (M);

  //
  // Insert the constructor into the list of global constructor functions.
  //
  insertIntoGlobalCtorList (RuntimeCtor);

  //
  // Instrument the function to record it's thread id, 
  // if it is a function started from pthread_create
  //
  // Test handler function
  instrumentPthreadCreatedFunctions ( M.getFunction("PrintHello") );
  // MySQL handler Function
  instrumentPthreadCreatedFunctions ( M.getFunction("handle_one_connection") );

  //
  // Indicate that we've changed the module.
  //
  return true;
}

//
// Method: instrumentBasicBlock()
//
// Description:
//  This method instruments a basic block so that it records its execution at
//  run-time.
//
void giri::TracingNoGiri::instrumentBasicBlock (BasicBlock & BB) {

  Value *LastBB;

  //
  // Lookup the ID of this basic block and create an LLVM value for it.
  //
  unsigned id = bbNumPass->getID (&BB);
  assert (id && "Basic block does not have an ID!\n");
  Value * BBID = ConstantInt::get(Int32Type, id);

  //
  // Get a pointer to the function in which the basic block belongs.
  //
  Value * FP = castTo (BB.getParent(), VoidPtrType, "", BB.getTerminator());

  
  if ( dyn_cast<ReturnInst> (BB.getTerminator()) )
     LastBB = ConstantInt::get(Int32Type, 1);                            
  else
     LastBB = ConstantInt::get(Int32Type, 0);                            
 

  //
  // Insert code at the end of the basic block to record that it was executed.
  //
  std::vector<Value *> args = make_vector<Value *>(BBID, FP, LastBB, 0);
  CallInst::Create (RecordBB, args, "", BB.getTerminator());

  // For debugging
#if 0
  if( BB.getParent()->getNameStr().compare("ap_rprintf") == 0 ) {
    printf("Debugging call record: %lu %lx\n", (ulong)BB.getParent(), (ulong)BB.getParent());
  }
#endif

  //
  // Insert code at the beginning of the basic block to record that it started
  // execution.
  //
  args = make_vector<Value *>(BBID, FP, 0);
  CallInst::Create (RecordStartBB, args, "", BB.getFirstInsertionPt());
  
  return;
}

//
// Method: visitLoadInst()
//
// Description:
//  Visit a load instruction.  This method instruments the load instruction
//  with a call to the tracing run-time that will record, in the dynamic trace,
//  the memory read by this load instruction.
//
void giri::TracingNoGiri::visitLoadInst (LoadInst & LI) {
  //
  // Cast the pointer into a void pointer type.
  //
  Value * Pointer = LI.getPointerOperand();
  Pointer = castTo (Pointer, VoidPtrType, Pointer->getName(), &LI);

  //
  // Get the size of the loaded data.
  //
  uint64_t size = TD->getTypeStoreSize (LI.getType());
  Value * LoadSize = ConstantInt::get(Int64Type, size); 

  //
  // Get the ID of the load instruction.
  //
  Value * LoadID = ConstantInt::get(Int32Type, lsNumPass->getID (&LI));

  //
  // Create the call to the run-time to record the load instruction.
  //
  std::vector<Value *> args=make_vector<Value *>(LoadID, Pointer, LoadSize, 0);
  CallInst::Create (RecordLoad, args, "", &LI);

  // Update statistics
  ++Loads;
}

//
// Method: visitSelectInst()
//
// Description:
//  Visit a select instruction.  This method instruments the select instruction
//  with a call to the tracing run-time that will record, in the dynamic trace,
//  the boolean value that the select instruction will use to select its output
//  operand.
//
void giri::TracingNoGiri::visitSelectInst (SelectInst & SI) {
  //
  // Cast the predicate (boolean) value into an 8-bit value.
  //
  Value * Predicate = SI.getCondition();
  Predicate = castTo (Predicate, Int8Type, Predicate->getName(), &SI);

  //
  // Get the ID of the load instruction.
  //
  Value * SelectID = ConstantInt::get(Int32Type, lsNumPass->getID (&SI));

  //
  // Create the call to the run-time to record the load instruction.
  //
  std::vector<Value *> args=make_vector<Value *>(SelectID, Predicate, 0);
  CallInst::Create (RecordSelect, args, "", &SI);

  // Update statistics
  ++Selects;
  return;
}

//
// Method: visitStoreInst()
//
// Description:
//  Visit a store instruction.  This method instruments the store instruction
//  with a call to the tracing run-time that will record, in the dynamic trace,
//  the memory written by this store instruction.
//
void giri::TracingNoGiri::visitStoreInst (StoreInst & SI) {
  //
  // Cast the pointer into a void pointer type.
  //
  Value * Pointer = SI.getPointerOperand();
  Pointer = castTo (Pointer, VoidPtrType, Pointer->getName(), &SI);

  //
  // Get the size of the stored data.
  //
  uint64_t size = TD->getTypeStoreSize (SI.getOperand(0)->getType());
  Value * StoreSize = ConstantInt::get(Int64Type, size); 

  //
  // Get the ID of the store instruction.
  //
  Value * StoreID = ConstantInt::get(Int32Type, lsNumPass->getID (&SI));

  //
  // Create the call to the run-time to record the store instruction.
  //
  std::vector<Value *> args=make_vector<Value *>(StoreID,Pointer,StoreSize,0);
  CallInst::Create (RecordStore, args, "", &SI);

  // Update statistics
  ++Stores;
}

//
// Method: visitSpecialCall()
//
// Description:
//  Examine a call instruction and see if it is a call to an external function
//  which is treated specially by the dynamic slicing code.  If so, instrument
//  it with the appropriate calls to the run-time.
//
// Inputs:
//  CI - The call instruction which may call a special function.
//
// Return value:
//  true  - This call does call a special call instruction.
//  false - This call does not call a special call instruction.
//
bool giri::TracingNoGiri::visitSpecialCall (CallInst & CI) {
  //
  // We do not support indirect calls to special functions.
  //
  Value * NumElts;
  Function * CalledFunc = CI.getCalledFunction();
  if (CalledFunc == NULL)
    return false;
  CalledFunc = CalledFunc;

  //
  // Do not consider a function special if it has a function body; in this
  // case, the programmer has supplied his or her version of the function, and
  // we will instrument it.
  //
  if (!(CalledFunc->isDeclaration()))
    return false;

  //
  // Check the name of the function against a list of known special functions.
  //
  std::string name = CalledFunc->getName().str();

  if (name.substr(0,12) == "llvm.memset." /* "memset" */ ) {
    //
    // Get the destination pointer and cast it to a void pointer.
    //
    Value * dstPointer = CI.getOperand(0);
    dstPointer = castTo (dstPointer, VoidPtrType, dstPointer->getName(), &CI);

    //
    // Get the number of bytes that will be written into the buffer.
    //
    NumElts = CI.getOperand(2);

    //
    // Get the ID of the external funtion call instruction.
    //
    Value * CallID = ConstantInt::get(Int32Type, lsNumPass->getID (&CI));

    //
    // Create the call to the run-time to record the external call instruction.
    //
    std::vector<Value *> args = make_vector (CallID, dstPointer, NumElts, 0);
    CallInst::Create (RecordStore, args, "", &CI);

    // Update statistics
    ++ExtFuns;
    return true;

  } else if (name.substr(0,12) == "llvm.memcpy." /*"memcpy"*/ || \
             name.substr(0,13) == "llvm.memmove." /*"memmove"*/ || \
             name == "strcpy") {  /* Record Load src, [CI] Load dst [CI] */
    //
    // Get the destination and source pointers and cast them to void pointers.
    //
    Value * dstPointer = CI.getOperand(0);
    Value * srcPointer  = CI.getOperand(1);
    dstPointer = castTo (dstPointer, VoidPtrType, dstPointer->getName(), &CI);
    srcPointer  = castTo (srcPointer,  VoidPtrType, srcPointer->getName(), &CI);

    // Get the ID of the ext fun call instruction.
    Value * CallID = ConstantInt::get(Int32Type, lsNumPass->getID (&CI));

    // Create the call to the run-time to record the loads and stores of 
    // external call instruction.
    if(name == "strcpy") {
      // CHECK: If the tracer function should be inserted before or after????
      std::vector<Value *> args = make_vector (CallID, srcPointer, 0);
      CallInst::Create (RecordStrLoad, args, "", &CI);

      args = make_vector (CallID, dstPointer, 0);
      CallInst *recStore = CallInst::Create (RecordStrStore, args, "", &CI);
      CI.moveBefore(recStore);      
    } else {
      // get the num elements to be transfered
      NumElts = CI.getOperand(2);

      std::vector<Value *> args = make_vector (CallID, srcPointer, NumElts, 0);
      CallInst::Create (RecordLoad, args, "", &CI);

      args = make_vector (CallID, dstPointer, NumElts, 0);
      CallInst::Create (RecordStore, args, "", &CI);
    }

    // Update statistics
    ++ExtFuns;
    return true;

  } else if (name == "strcat") { /* Record Load dst, Load Src, Store dst-end before call inst  */
    //
    // Get the destination and source pointers and cast them to void pointers.
    //
    Value * dstPointer = CI.getOperand(0);
    Value * srcPointer  = CI.getOperand(1);
    dstPointer = castTo (dstPointer, VoidPtrType, dstPointer->getName(), &CI);
    srcPointer  = castTo (srcPointer,  VoidPtrType, srcPointer->getName(), &CI);

    // Get the ID of the ext fun call instruction.
    Value * CallID = ConstantInt::get(Int32Type, lsNumPass->getID (&CI));

    // Create the call to the run-time to record the loads and stores of 
    // external call instruction.
    // CHECK: If the tracer function should be inserted before or after????
    std::vector<Value *> args = make_vector (CallID, dstPointer, 0);
    CallInst::Create (RecordStrLoad, args, "", &CI);

    args = make_vector (CallID, srcPointer, 0);
    CallInst::Create (RecordStrLoad, args, "", &CI);

    // Record the addresses before concat as they will be lost after concat
    args = make_vector (CallID, dstPointer, srcPointer, 0);
    CallInst::Create (RecordStrcatStore, args, "", &CI);

    // Update statistics
    ++ExtFuns;
    return true;


  } else if (name == "strlen") { /* Record Load */
    //
    // Get the destination and source pointers and cast them to void pointers.
    //
    Value * srcPointer  = CI.getOperand(0);
    srcPointer  = castTo (srcPointer,  VoidPtrType, srcPointer->getName(), &CI);

    // Get the ID of the ext fun call instruction.
    Value * CallID = ConstantInt::get(Int32Type, lsNumPass->getID (&CI));

    std::vector<Value *> args = make_vector (CallID, srcPointer, 0);
    CallInst::Create (RecordStrLoad, args, "", &CI);

    // Update statistics
    ++ExtFuns;
    return true;

  } else if (name == "calloc") {
    //
    // Get the number of bytes that will be written into the buffer.
    //
    NumElts = BinaryOperator::Create (BinaryOperator::Mul, CI.getOperand(0), CI.getOperand(1), "calloc par1 * par2", &CI);

    //
    // Get the destination pointer and cast it to a void pointer.
    //
    //Instruction * dstPointerInst;
    Value * dstPointer = castTo (&CI, VoidPtrType, CI.getName(), &CI);

    /* // To move after call inst, we need to know if cast is a constant expr or inst
    if ( (dstPointerInst = dyn_cast<Instruction>(dstPointer)) ) { 
        CI.moveBefore(dstPointerInst); // dstPointerInst->insertAfter(&CI);
        // ((Instruction *)NumElts)->insertAfter(dstPointerInst);
    } 
    else {
        CI.moveBefore((Instruction *)NumElts); // ((Instruction *)NumElts)->insertAfter(&CI);
	}
    dstPointer = dstPointerInst; // Assign to dstPointer for instrn or non-instrn values
    */

    //
    // Get the ID of the external funtion call instruction.
    //
    Value * CallID = ConstantInt::get(Int32Type, lsNumPass->getID (&CI));

    //
    // Create the call to the run-time to record the external call instruction.
    //
    std::vector<Value *> args = make_vector (CallID, dstPointer, NumElts, 0);
    CallInst *recStore = CallInst::Create (RecordStore, args, "", &CI);
    CI.moveBefore(recStore); //recStore->insertAfter((Instruction *)NumElts);

    // Moove cast, #byte computation and store to after call inst
    CI.moveBefore((Instruction *)NumElts); 

    // Update statistics
    ++ExtFuns;
    return true;

  } else if (name == "tolower" || name == "toupper") {
    // Not needed as there are no loads and stores

  /*  } else if (name == "strncpy/itoa/stdarg/scanf/fscanf/sscanf/fread/complex/strftime/strptime/asctime/ctime") { */

  } else if (name == "fscanf") {
    // TO DO
    // In stead of parsing format string, can we use the type of the arguments??
  } else if (name == "sscanf") {
    // TO DO

  } else if (name == "sprintf") {
    //
    // Get the pointer to the destination buffer.
    //
    Value * dstPointer = CI.getOperand(0);
    dstPointer = castTo (dstPointer, VoidPtrType, dstPointer->getName(), &CI);

    //
    // Get the ID of the call instruction.
    //
    Value * CallID = ConstantInt::get(Int32Type, lsNumPass->getID (&CI));

    //
    // Scan through the arguments looking for what appears to be a character
    // string.  Generate load records for each of these strings.
    //
    for (unsigned index = 2; index < CI.getNumOperands(); ++index) {
      if (CI.getOperand(index)->getType() == VoidPtrType) {
        //
        // Create the call to the run-time to record the load from the string.
        // What about other loads??
        //
        Value * Ptr = CI.getOperand(index);
        std::vector<Value *> args = make_vector (CallID, Ptr, 0);
        CallInst::Create (RecordStrLoad, args, "", &CI);

        // Update statistics
        ++LoadStrings;
      }
    }

    //
    // Create the call to the run-time to record the external call instruction.
    //
    std::vector<Value *> args = make_vector (CallID, dstPointer, 0);
    CallInst *recStore = CallInst::Create (RecordStrStore, args, "", &CI);
    CI.moveBefore(recStore);

    // Update statistics
    ++StoreStrings;
    return true;

  } else if (name == "fgets") {
    //
    // Get the pointer to the destination buffer.
    //
    Value * dstPointer = CI.getOperand(0);
    dstPointer = castTo (dstPointer, VoidPtrType, dstPointer->getName(), &CI);

    //
    // Get the ID of the ext fun call instruction.
    //
    Value * CallID = ConstantInt::get(Int32Type, lsNumPass->getID (&CI));

    //
    // Create the call to the run-time to record the external call instruction.
    //
    std::vector<Value *> args = make_vector (CallID, dstPointer, 0);
    CallInst *recStore = CallInst::Create (RecordStrStore, args, "", &CI);
    CI.moveBefore(recStore);

    // Update statistics
    ++StoreStrings;
    return true;
  }

  return false;
}

//
// Method: visitCallInst()
//
// Description:
//  Visit a call instruction.  For most call instructions, we will instrument
//  the call so that the trace contains enough information to track back from
//  the callee to the caller.  For some call instructions, we will emit special 
//  instrumentation to record the memory read and/or written by the call instruction. 
//  Also call records are needed to map invariant failures to call insts.
//
void giri::TracingNoGiri::visitCallInst  (CallInst & CI) {

  CallInst *ClInst;

  //
  // Attempt to get the called function.
  //      
  Function * CalledFunc = CI.getCalledFunction();

  //
  // Do not instrument calls to tracing run-time functions or debug functions.
  //
  if (isTracerFunction(CalledFunc))
    return;

  if (CalledFunc) {  
    if(CalledFunc->getName().str().compare(0,9,"llvm.dbg.") == 0 )
      return; 
  }

  // Now, need to record call and return records of external calls
  // to mark invariant failures correctly
  //
  /*

  //
  // There are some calls to external functions that we handle specially.
  // Take care of those now.
  //
  if (visitSpecialCall (CI))
    return;
        
  //
  // If the call is not a special call but directly calls an external function,
  // don't instrument it.
  //
  */
  
  // Instrument external calls which can have invariants on its return value
  if (CalledFunc && (CalledFunc->isDeclaration())) {
     // No need to record calls of functions on which there are no invariants
     // Do not instrument calls to intrinsic functions as it is giving an assert failure.
     // FIX ME!!!! may miss some invariant failures, not likely as none of the intrinsics 
     // have invariants
    if ( CalledFunc->isIntrinsic () || !checkForInvariantInst(&CI)  ) {
       // Instrument special external calls which loads/stores 
       // like strlen, strcpy, memcpy etc.
       visitSpecialCall (CI);
       return;
    }
  
     //if(CalledFunc->getNameStr() != "pthread_create")
     // return;
  }


  //
  // If the called value is inline assembly code, then don't instrument it.
  //
  if (isa<InlineAsm>(CI.getCalledValue()->stripPointerCasts()))
    return;

  //
  // Get the ID of the store instruction.
  //
  Value * CallID = ConstantInt::get(Int32Type, lsNumPass->getID (&CI));

  //
  // Get the called function value and cast it to a void pointer.
  //
  Value * FP = castTo (CI.getCalledValue(), VoidPtrType, "", &CI);

  // For debugging
#if 0
  if( CI.getCalledValue()->getNameStr().compare("ap_rprintf") == 0 ) {
    printf("Debugging call record: %lu %lx\n", (ulong)CI.getCalledValue(), (ulong)CI.getCalledValue());
  }
#endif

  //
  // Create the call to the run-time to record the call instruction.
  //
  std::vector<Value *> args = make_vector<Value *>(CallID, FP, 0);

  // Do not add calls to function call stack for external functions
  // as return records won't be used/needed for them, so call a special record function
  // FIX IT!!!! Do we still need it after adding separate return records????
  if( CalledFunc && CalledFunc->getName().str() == "pthread_create")
    CallInst::Create (RecordExtCall, args, "", &CI);
  else 
    CallInst::Create (RecordCall, args, "", &CI);

  // Update statistics
  ++Calls;

  //
  // Create the call to the run-time to record the return of call instruction.
  //
  ClInst = CallInst::Create (RecordReturn, args, "", &CI);
  CI.moveBefore(ClInst);

  // The best way to handle external call is to set a flag before calling ext fn and
  // use that to determine if an internal function is called from ext fn. It flag can be 
  // reset afterwards and restored to its original value before returning to ext code.
  // FIX ME!!!! LATER

  if (CalledFunc && (CalledFunc->isDeclaration())) {

    // If pthread_create is called then handle it specially as it calls
    // functions externally and add an extra call for the externally 
    // called functions with the same id so that returns can match with it.
    // In addition to a function call to pthread_create.

    if(CalledFunc->getName().str() == "pthread_create") {
   
    //
    // Get the function pointer operand which will be called externally  and cast it to a void pointer.
    //
    Value * FP = castTo (CI.getOperand(2), VoidPtrType, "", &CI);

    //
    // Create the call to the run-time to record the call instruction.
    //
    std::vector<Value *> argsExt = make_vector<Value *>(CallID, FP, 0);
    ClInst = CallInst::Create (RecordCall, argsExt, "", &CI);
    CI.moveBefore(ClInst);
       
    // Update statistics
    ++Calls;

    // For, both external functions and internal/ext functions called from
    // external functions, return records are not useful as they won't be used.
    // Since, we won't create return records for them, simply update the call 
    // stack to mark the end of function call.

    //args = make_vector<Value *>(CallID, FP, 0);
    //CallInst::Create (RecordExtCallRet, args.begin(), args.end(), "", &CI);

    //
    // Create the call to the run-time to record the return of call instruction.
    //
    CallInst::Create (RecordReturn, argsExt, "", &CI);
    }

  }
 
  // Instrument special external calls which loads/stores 
  // like strlen, strcpy, memcpy etc.
  visitSpecialCall (CI);

  return;
}

//
// Method: instrumentLoadsAndStores()
//
// Description:
//  This method instruments loads and stores so that they record the memory
//  addresses that they are accessing.  Record Calls and addresses of special 
//  external functions as well.
void giri::TracingNoGiri::instrumentLoadsAndStores (BasicBlock & BB) {
  //
  // Scan through all instructions in the basic block and instrument them as
  // necessary.  Use a worklist to contain the instructions to avoid any
  // iterator invalidation issues when adding instructions to the basic block.
  //
  std::vector<Instruction *> Worklist;
  for (BasicBlock::iterator I = BB.begin(); I != BB.end(); ++I) {
    Worklist.push_back (I);
  }
  visit (Worklist.begin(), Worklist.end());
  return;
}

//
// Method:   instrumentPthreadCreatedFunctions ()
//
// Description:
// Instrument the function to record it's thread id, 
// if it is a function started from pthread_create
//
void giri::TracingNoGiri::instrumentPthreadCreatedFunctions (Function *F) {

  // If no such handler function exist, the return
  if( F == NULL )
    return;
  
  Module & M = *(F->getParent());

  //
  // Insert code at the beginning of the Function to record the thread id.
  //
  Constant * Name = stringToGV (F->getName().str(), &M);
  Name = ConstantExpr::getZExtOrBitCast (Name, VoidPtrType);
  CallInst::Create (RecordHandlerThreadID, Name, "", F->getEntryBlock().getFirstInsertionPt());

  return;
}


//
// Method: runOnBasicBlock()
//
// Description:
//  This method starts execution of the dynamic slice tracing instrumentation
//  pass.  It will add code to a function that records the execution of basic
//  blocks.
//
bool giri::TracingNoGiri::runOnBasicBlock (BasicBlock & BB) {
  //
  // Fetch the analysis results for numbering basic blocks.
  //
  // Will be run once per module
  TD        = &getAnalysis<TargetData>();
  bbNumPass = &getAnalysis<QueryBasicBlockNumbers>();
  lsNumPass = &getAnalysis<QueryLoadStoreNumbers>();

  //
  // Instrument the basic block so that it records its execution.
  //
  instrumentBasicBlock (BB);

  //
  // Instrument all load and store instructions so that they record their
  // execution and the address that they access. Record Calls and addresses of
  // special external functions as well.
  instrumentLoadsAndStores (BB);

  //
  // Update the number of basic blocks with phis.
  //
  if (hasPHI (BB)) ++PHIBBs;

  //
  // Update the number of basic blocks.
  //
  ++NumBBs;

  //
  // Assume that we modified something.
  //
  return true;
}

//
// Method: initialize()
//
// Description:
//  Initialize type objects used for invarint inst checking.
//
// Inputs:
//  M - The module to analyze.
void giri::TracingNoGiri::initialize (Module & M)
{
  /*** Create the type variables ***/
  ////////////////////  Right now treat all unsigned values as signed
  /*
  UInt64Ty = Type::getInt64Ty(M.getContext());
  UInt32Ty = Type::getInt32Ty(M.getContext());
  UInt16Ty = Type::getInt16Ty(M.getContext());
  UInt8Ty  = Type::getInt8Ty(M.getContext());
  */
  SInt64Ty = Type::getInt64Ty(M.getContext());
  SInt32Ty = Type::getInt32Ty(M.getContext());
  SInt16Ty = Type::getInt16Ty(M.getContext());
  SInt8Ty  = Type::getInt8Ty(M.getContext());
  FloatTy  = Type::getFloatTy(M.getContext());
  DoubleTy = Type::getDoubleTy(M.getContext());
}

bool  giri::TracingNoGiri::checkType(const Type *T) {
  if( T == SInt64Ty || T == SInt32Ty || T == SInt16Ty || T == SInt8Ty )
    return true;
  //if( !NO_UNSIGNED_CHECK )
  if( T == UInt64Ty || T == UInt32Ty || T == SInt16Ty || T == SInt8Ty )
      return true; 
  //if( !NO_FLOAT_CHECK )
  if( T == FloatTy || T == DoubleTy )
      return true;
 
  return false;
}

bool giri::TracingNoGiri::checkForInvariantInst(Value *V)
{
  Value *CheckVal;

  if( CallInst *ClInst = dyn_cast<CallInst>(V) ) 
    {
      CheckVal = ClInst; // Get the value to be checked

      if ( ClInst->getCalledFunction() != NULL )
        {
         if (isTracerFunction(ClInst->getCalledFunction())) // Don't count and check the tracer/inv functions
            return false;
        }

      // If this instruction is in the slice, then the corresponding invariant must have executed successfully or failed
      if( checkType(CheckVal->getType()) && isa<Constant>(*CheckVal) == false )
        return true;
    }

  else if( StoreInst *StInst = dyn_cast<StoreInst>(V) ) 
    {
      DEBUG( std::cerr << "Handling store instruction \n" );
      CheckVal = StInst->getOperand(0);  // Get value operand               
      
      // If this instruction is in the slice, then the corresponding invariant must have executed successfully or failed
      if( checkType(CheckVal->getType()) && isa<Constant>(*CheckVal) == false )
        return true; 
    }
  else if( LoadInst *LdInst = dyn_cast<LoadInst>(V) ) 
    {
      DEBUG( std::cerr << "Handling load instruction \n" );
      CheckVal = LdInst;  // Get value operand               
      
      // If this instruction is in the slice, then the corresponding invariant must have executed successfully or failed
      if( checkType(CheckVal->getType()) && isa<Constant>(*CheckVal) == false ) 
        return true;
    }

  return false; // All other instructions do not have invariants
}
