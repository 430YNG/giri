//===- Tracing.cpp - Implementation of dynamic slicing tracing runtime ----===//
// 
//                          Bug Diagnosis Compiler
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements the run-time functions for tracing program execution.
// It is specifically designed for tracing events needed for performing dynamic
// slicing.
//
// Note that this code is *not* thread-safe!
//
//===----------------------------------------------------------------------===//

#include "Giri/Runtime.h"

#include <cstdio>
#include <cassert>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

//using namespace std;

extern "C" void recordInit  (const char * name);
extern "C" void recordStartBB (unsigned id, unsigned char * fp);
extern "C" void recordBB    (unsigned id, unsigned char * fp, unsigned lastBB);
extern "C" void recordLoad  (unsigned id, unsigned char * p, uintptr_t);
extern "C" void recordStore (unsigned id, unsigned char * p, uintptr_t);
extern "C" void recordStrLoad (unsigned id, char * p);
extern "C" void recordStrStore (unsigned id, char * p);
extern "C" void recordStrcatStore (unsigned id, char * p, char * s);
extern "C" void recordCall  (unsigned id, unsigned char * p);
extern "C" void recordReturn  (unsigned id, unsigned char * p);
extern "C" void recordExtCall  (unsigned id, unsigned char * p);
extern "C" void recordExtFun  (unsigned id);
extern "C" void recordExtCallRet (unsigned callID, unsigned char * fp);
extern "C" void recordInvFailure  (unsigned id);
extern "C" void recordSelect  (unsigned id, unsigned char flag);
extern "C" void flushEntryCache (void);
extern "C" void closeCacheFile (void);
extern "C" void recordHandlerThreadID (const char * name);

// File for recording tracing information
static int record = 0;

// Stores the threadID for the thread which 
// handles incoming connections
static unsigned long handlerThreadID = 0;
static int countUpdateThreadID = 0;

// Mutex to protect concurrent read/writes to thread ID
pthread_mutex_t mutexRecordThreadID;

// A stack containing basic blocks currently being executed
static const unsigned maxBBStack = 4096;
static unsigned BBStackIndex = 0;
static struct {
  unsigned id;
  unsigned char * address;
} BBStack [maxBBStack];

static const unsigned maxFNStack = 4096;
static unsigned FNStackIndex = 0;
static struct {
  unsigned id;
  unsigned char * fnAddress;
} FNStack [maxFNStack];

// Size of the entry cache in bytes (currently 256 MB)
const unsigned entryCacheSizeinBytes = 256*1024*1024;

// Size of the entry cache
const unsigned entryCacheSize = entryCacheSizeinBytes / sizeof (Entry);

// The current index into the trace file.  This points to the next element
// in which to write the next entry in the trace file.
unsigned trace_index;

struct entryCache {
  // The current index into the entry cache.  This points to the next element
  // in which to write the next entry (cache holds a part of the trace file).
  unsigned index;

  // A cache of entries that need to be written to disk
  Entry * cache;

  // The offset into the file which is cached into memory.
  off_t fileOffset;

  // File which is being cached in memory.
  int fd;

  void openFD (int FD);
  void flushCache (void);
} entryCache;


// Update Connection Handler Thread ID
void updateThreadID( )
{
   pthread_mutex_lock (&mutexRecordThreadID);
   printf("Inside updateThreadID\n");
   handlerThreadID = pthread_self();
   countUpdateThreadID++;
   if( countUpdateThreadID > 2 )
     printf("WARNING!!! More threads than 1 Handler thread!! May cause error\n");
   pthread_mutex_unlock (&mutexRecordThreadID);
}

// Check if current thread is the Main thread / Connection Handler Thread or not
bool checkForNonHandlerThread( )
{
   return false; // Always record for all threads for other than MySQL

  // Only used for MySQL
#if 0
   bool flag;

   pthread_mutex_lock (&mutexRecordThreadID);
   if( pthread_self() == handlerThreadID  ) {
     //printf("Yes, Connection thread handler\n");
     flag = false;
   }
   else {
     //printf("No, Not a Connection thread handler\n");
     flag = true;
   }
   pthread_mutex_unlock (&mutexRecordThreadID);

   return flag;
#endif
}

// Signal handler to write only tracing data to file
void cleanup_only_tracing(int signum)
{
  fprintf(stderr, "Abnormal Termination, signal number %d\n", signum);

  // Right now call exit and do all cleaning operations through atexit
  /*
  #if 1   // Define only when doing slicing
      closeCacheFile (); // Make sure to close the file properly and flush the file cache
  #endif
  */
 
  exit(signum);
}

void entryCache::openFD (int FD) {
  // Save the file descriptor of the file that we'll use.
  fd = FD;

  //
  // Initialize all of the other fields.
  //
  index = 0;
  fileOffset = 0;
  cache = 0;

  //
  // Extend the size of the file so that we can access the data within it.
  //
#ifndef __CYGWIN__
  char buf[1] = {0};
  off_t currentPosition = lseek (fd, entryCacheSizeinBytes+1, SEEK_CUR);
  write (fd, buf, 1);
  lseek (fd, currentPosition, SEEK_SET);
#endif

  //
  // Map the first portion of the file into memory.  It will act as our
  // cache.
  //
#ifdef __CYGWIN__
  cache = (Entry *) mmap (0, entryCacheSizeinBytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_AUTOGROW, fd, fileOffset);
#else
  cache = (Entry *) mmap (0, entryCacheSizeinBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, fileOffset);
#endif
  assert (cache != MAP_FAILED);
}

void entryCache::flushCache (void) {
  //
  // Unmap the data.  This should force it to be written to disk.
  //
  msync (cache, entryCacheSizeinBytes, MS_SYNC);
  munmap (cache, sizeof(Entry) * index);

  //
  // Advance the file offset to the next portion of the file.
  //
  fileOffset += entryCacheSizeinBytes;

#ifndef __CYGWIN__
  char buf[1] = {0};
  off_t currentPosition = lseek (fd, entryCacheSizeinBytes+1, SEEK_CUR);
  write (fd, buf, 1);
  lseek (fd, currentPosition, SEEK_SET);
#endif

  //
  // Map in the next section of the file.
  //
#ifdef __CYGWIN__
  cache = (Entry *) mmap (0, entryCacheSizeinBytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_AUTOGROW, fd, fileOffset);
#else
  cache = (Entry *) mmap (0, entryCacheSizeinBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, fileOffset);
#endif
  assert (cache != MAP_FAILED);

  //
  // Reset the entry cache.
  //
  index = 0;
  return;
}

static inline void addToEntryCache (const Entry & entry) {
  //
  // Flush the cache if necessary.
  //
  if (entryCache.index == entryCacheSize) {
    flushEntryCache();
  }

  //
  // Add the entry to the entry cache.
  //
  entryCache.cache[entryCache.index] = entry;

  //
  // Increment the index for the next entry.
  //
  ++entryCache.index;

  // Increment the index for entry in the trace file
  ++trace_index;

  //
  // Initial experiments show that this increases overhead (user + system time).
  //
#if 0
  //
  // Tell the OS to sync if we've finished writing another page.
  //
  if ((uintptr_t)&(entryCache.cache[entryCache.index]) & 0x1000) {
    msync (&entryCache.cache[entryCache.index - 1], 1, MS_ASYNC);
  }
#endif

  return;
}

void flushEntryCache (void) {
  //
  // Flush the in-memory cache to disk.
  //
  entryCache.flushCache ();
  return;
}

void closeCacheFile (void) {
  fprintf(stdout, "Writing cache data to trace file and closing\n");
  //
  // Create basic block termination entries for each basic block on the stack.
  // These were the basic blocks that were active when the program terminated.
  // **** Should we print the return records for active functions as well?????????
  //
  while (BBStackIndex > 0) {
    //
    // Remove the item from the stack.
    //
    --BBStackIndex;

    //
    // Get the Basic Block ID at the top of the stack.
    //
    unsigned bbid      = BBStack[BBStackIndex].id;
    unsigned char * fp = BBStack[BBStackIndex].address;

    //
    // Create a basic block entry for it.
    //
    Entry entry (BBType, bbid, fp);
    addToEntryCache (entry);
  }

  //
  // Create an end entry to terminate the log.
  //
  Entry entry (ENType, 0);
  addToEntryCache (entry);

  //
  // Flush the entry cache.
  //
  flushEntryCache();
  return;
}

void recordInit  (const char * name) {
  // First assert that the size of an entry evenly divides the cache entry
  // buffer size.  The run-time will not work if this is not true.
  if (entryCacheSizeinBytes % sizeof (Entry)) {
    fprintf (stderr,
             "Entry size %lu does not divide cache size!\n",
             sizeof(Entry));
    abort();
  }

  // Open the file for recording the trace if it hasn't been opened already.
  // Truncate it in case this dynamic trace is shorter than the last one stored
  // in the file.
  record = open (name, O_RDWR | O_CREAT | O_TRUNC, 0640u);
  assert ((record != -1) && "Failed to open tracing file!\n");
  fprintf(stdout, "Opened trace file: %s\n", name);

  // Initialize the entry cache by giving it a memory buffer to use.
  entryCache.openFD (record);

  // Make sure that we flush the entry cache on exit.
  atexit (closeCacheFile);

  //Register the signal handlers for flushing of diagnosis tracing data to file
  signal(SIGINT, cleanup_only_tracing);
  signal(SIGQUIT, cleanup_only_tracing);
  signal(SIGSEGV, cleanup_only_tracing);
  signal(SIGABRT, cleanup_only_tracing);
  signal(SIGTERM, cleanup_only_tracing);
  signal(SIGKILL, cleanup_only_tracing);
  signal(SIGILL, cleanup_only_tracing);
  signal(SIGFPE, cleanup_only_tracing);

  pthread_mutex_init(&mutexRecordThreadID, NULL);

  // Update the thread id with the main thread ID
  updateThreadID();

  return;
}

/// Record that a basic block has started execution. This doesn't generate a
/// record in the log itself; rather, it is used to create records for basic
/// block termination if the program terminates before the basic blocks
/// complete execution.
void recordStartBB (unsigned id, unsigned char * fp) {
 
  // Don't record, if it is not the main thread or connection handler thread
  if( checkForNonHandlerThread() )
    return;

  if( id >= 190525 && id <= 190532 )
    fprintf(stdout, "At BasicBlock start, BBid %u between 190525 and 190532\n",
            id);

  // Ensure that we have enough space on the basic block stack.
  if (BBStackIndex == maxBBStack) {
    fprintf(stderr, "Basic Block Stack overflowed in Tracing runtime\n");
    abort();
  }

  // Push the basic block identifier on to the back of the stack.
  BBStack[BBStackIndex].id = id;
  BBStack[BBStackIndex++].address = fp;
  
  // FIXME: Special handling for clang code
  if( id == 190531 ) {
    fprintf(stderr, "Due to some bug, some entries r missing in trace. \
                     Hence force writing trace file here at symptom\n");
    closeCacheFile();
    abort();
  }

  return;
}

/// Record that a basic block has finished execution.
/// \param id - The ID of the basic block that has finished execution.
/// \param fp - The pointer to the function in which the basic block belongs.
void recordBB (unsigned id, unsigned char * fp, unsigned lastBB) {

  // Don't record, if it is not the main thread or connection handler thread
  if( checkForNonHandlerThread() )
    return;

  if( id >= 190525 && id <= 190532 )
    fprintf(stdout, "At BasicBlock end, BBid %u between 190525 and 190532\n", id);
  
  // Record that this basic block as been executed.
  unsigned callID = 0;

  // If this is the last BB of this function invocation, take the Function id
  // off the Function stack stack. We have recorded that it has finished
  // execution. Store the call id to record the end of function call at the end
  // of the last BB.
  if (lastBB) {
    if (FNStackIndex > 0 ) {   
      if (FNStack[FNStackIndex - 1].fnAddress != fp ) {
        fprintf(stderr, "Function id on stack doesn't match for id %u.\
                         MAY be due to function call from external code\n", id);
      } else {
        --FNStackIndex;
        callID = FNStack[FNStackIndex].id;
      }
    } else {
      // If nothing in stack, it is main function return which doesn't have a matching call.
      // Hence just store a large number as call id
      callID = ~0;
    }
  } else {
    callID = 0;
  }
    
  Entry entry(BBType, id, fp, callID);
  addToEntryCache (entry);

  // Take the basic block off the basic block stack.  We have recorded that it
  // has finished execution.
  --BBStackIndex;

  return;
}

// TODO: delete this (**Not needed anymore as we don't add external function call records**)
///  Record that a external function has finished execution by updating function call stack.
void recordExtCallRet (unsigned callID, unsigned char * fp) {

  // Don't record, if it is not the main thread or connection handler thread
  if( checkForNonHandlerThread() )
    return;

  assert( FNStackIndex > 0 ); 

  fprintf(stdout, "Inside recordExtCallRet: %u %s %lx\n",
          callID, fp, FNStack[FNStackIndex - 1].fnAddress);
  
  if( FNStack[FNStackIndex - 1].fnAddress != fp )
	fprintf(stderr, "Function id on stack doesn't match for id %u. \
                     MAY be due to function call from external code\n", callID);
  else
     --FNStackIndex;

  return;
}

void recordLoad (unsigned id, unsigned char * p, uintptr_t length) {
  // Don't record, if it is not the main thread or connection handler thread
  if( checkForNonHandlerThread() )
    return;

  // Record that a load has been executed.
  Entry entry (LDType, id, p, length);
  addToEntryCache (entry);

  return;
}

/// Record that a store has occurred.
/// \param id     - The ID assigned to the store instruction in the LLVM IR.
/// \param p      - The starting address of the store.
/// \param length - The length, in bytes, of the stored data.
void recordStore (unsigned id, unsigned char * p, uintptr_t length) {

  // Don't record, if it is not the main thread or connection handler thread
  if( checkForNonHandlerThread() )
    return;

  // Record that a store has been executed.
  Entry entry (STType, id, p, length);
  addToEntryCache (entry);

  return;
}

///  Record that a string has been read.
void recordStrLoad (unsigned id, char * p) {

  // Don't record, if it is not the main thread or connection handler thread
  if( checkForNonHandlerThread() )
    return;

  //
  // First determine the length of the string.  Add one byte to include the
  // string terminator character.
  //
  uintptr_t length = strlen (p) + 1;

  //
  // Record that a load has been executed.
  //
  Entry entry (LDType, id, (unsigned char *) p, length);
  addToEntryCache (entry);

  return;
}

/// Record that a string has been written.
/// \param id - The ID of the instruction that wrote to the string.
/// \param p  - A pointer to the string.
void recordStrStore (unsigned id, char * p) {

  // Don't record, if it is not the main thread or connection handler thread
  if( checkForNonHandlerThread() )
    return;

  //
  // First determine the length of the string.  Add one byte to include the
  // string terminator character.
  //
  uintptr_t length = strlen (p) + 1;

  //
  // Record that there has been a store starting at the first address of the
  // string and continuing for the length of the string.
  //
  Entry entry (STType, id, (unsigned char *) p, length);
  addToEntryCache (entry);

  return;
}

/// Record that a string has been written on strcat.
/// \param id - The ID of the instruction that wrote to the string.
/// \param  p  - A pointer to the string.
void recordStrcatStore (unsigned id, char * p, char * s) {

  // Don't record, if it is not the main thread or connection handler thread
  if( checkForNonHandlerThread() )
    return;

  //
  // Determine where the new string will be added Don't. add one byte
  // to include the string terminator character, as write will start
  // from there. Then determine the length of the written string.
  //
  char *start = p + strlen (p);
  uintptr_t length = strlen (s) + 1;

  //
  // Record that there has been a store starting at the firstlast
  // address (the position of null termination char) of the string and
  // continuing for the length of the source string.
  //
  Entry entry (STType, id, (unsigned char *) start, length);
  addToEntryCache (entry);

  return;
}

/// Record that a call instruction was executed.
/// \param id - The ID of the call instruction.
/// \param fp - The address of the function that was called.
void recordCall (unsigned id, unsigned char * fp) {

  // Don't record, if it is not the main thread or connection handler thread
  if( checkForNonHandlerThread() )
    return;

  //
  // Record that a call has been executed.
  //
  Entry entry (CLType, id, fp);
  addToEntryCache (entry);

  // NOT needed anymore, REMOVE
  // Ensure that we have enough space on the Function call stack.
  //
  if (FNStackIndex == maxFNStack) {
    fprintf(stderr, "Function call Stack overflowed in Tracing runtime\n");
    //abort();
    return;
  }

  //
  // Push the Function call identifier on to the back of the stack.
  //
  FNStack[FNStackIndex].id = id;
  FNStack[FNStackIndex].fnAddress = fp;
  FNStackIndex++;

  return;
}

// FIXME: Do we still need it after adding separate return records????
/// Record that an external call instruction was executed.
/// \param id - The ID of the call instruction.
/// \param fp - The address of the function that was called.
void recordExtCall (unsigned id, unsigned char * fp) {

  // Don't record, if it is not the main thread or connection handler thread
  if( checkForNonHandlerThread() )
    return;

  fprintf(stdout, " Inside recordExtCall: %u %lx\n", id, fp);

  // Record that a call has been executed.
  Entry entry (CLType, id, fp);
  addToEntryCache (entry);

  return;
}

/// Record that a function has finished execution by adding a return trace entry
void recordReturn (unsigned id, unsigned char * fp) {

  // Don't record, if it is not the main thread or connection handler thread
  if( checkForNonHandlerThread() )
    return;

  //
  // Record that a call has returned.
  //
  Entry entry (RTType, id, fp);
  addToEntryCache (entry);

  return;
}

/// Record id of the failed invariant.
/// \param id - The ID assigned to the corresponding instruction in the LLVM IR
void recordInvFailure (unsigned id) {

  // Don't record, if it is not the main thread or connection handler thread
  if( checkForNonHandlerThread() )
    return;

  //
  // Record that an invariant has failed.
  //
  Entry entry (INVType, id);
  addToEntryCache (entry);

  return;
}

/// This function records which input of a select instruction was selected.
/// \param id - The ID assigned to the corresponding instruction in the LLVM IR
/// \param flag - The boolean value (true or false) used to determine the select
///               instruction's output.
void recordSelect  (unsigned id, unsigned char flag) {

  // Don't record, if it is not the main thread or connection handler thread
  if( checkForNonHandlerThread() )
    return;

  //
  // Record that a store has been executed.
  //
  Entry entry (PDType, id, (unsigned char *) flag);
  addToEntryCache (entry);

  return;
}

/// This function keeps track of the main thread id or the connection handler
/// thread ID.
/// \param name - The name of the connection handler function.
void recordHandlerThreadID (const char * name) {

  printf("Inside Connection Handling function %s\n", name);

  // Update the thread id with the main or connection handler thread ID
  updateThreadID();
}
