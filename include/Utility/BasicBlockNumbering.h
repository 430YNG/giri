//===- BasicBlockNumbering.h - Provide BB identifiers -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class provides LLVM passes that provide a *stable* numbering of basic
// blocks that does not depend on their address in memory (which is
// nondeterministic).
//
//===----------------------------------------------------------------------===//

#ifndef DG_BASICBLOCKNUMBERING_H
#define DG_BASICBLOCKNUMBERING_H

#include "llvm/Function.h"
#include "llvm/Module.h"
#include "llvm/Pass.h"

#include <map>

using namespace llvm;

namespace dg {
  //
  // Pass: BasicBlockNumberPass
  //
  // Description:
  //  This pass adds metadata to an LLVM module to assign a unique, stable ID
  //  to each basic block.
  //
  // Notes:
  //  This pass adds metadata that cannot be written to disk using the LLVM
  //  BitcodeWriter pass.
  //
  class BasicBlockNumberPass : public ModulePass {
    public:
      static char ID;
      BasicBlockNumberPass () : ModulePass (ID) {}
      virtual bool runOnModule (Module & M);
      virtual void getAnalysisUsage(AnalysisUsage &AU) const {
        AU.setPreservesCFG();
      };
    private:
      MDNode * assignIDToBlock (BasicBlock * BB, unsigned id);
  };

  //
  // Pass: QueryBasicBlockNumbers
  //
  // Description:
  //  This pass is an analysis pass that reads the metadata added by the
  //  BasicBlockNumberPass.  This pass makes querying the information easier
  //  for other passes and centralizes the reading of the metadata information.
  //
  class QueryBasicBlockNumbers : public ModulePass {
    public:
      static char ID;
      QueryBasicBlockNumbers () : ModulePass (ID) {}
      virtual bool runOnModule (Module & M);
      virtual void getAnalysisUsage(AnalysisUsage &AU) const {
        AU.setPreservesAll();
      };

      //
      // Method: getID()
      //
      // Description:
      //  Return the ID number for the specified BasicBlock.
      //
      // Return value:
      //  0 - This basic block has *no* associated ID.
      //  Otherwise, the ID of the basic block is returned.
      //
      unsigned getID (BasicBlock *BB) const {
        std::map<BasicBlock*, unsigned>::const_iterator I = IDMap.find(BB);
        if (I == IDMap.end())
          return 0;
        return I->second;
      }

      //
      // Method: getBlock()
      //
      // Description:
      //  Find the basic block associated with this ID.
      //
      // Inputs:
      //  id - The ID of the basic block.
      //
      // Return value:
      //  0 - No basic block has this ID.
      //  Otherwise, a pointer to the first basic block with this ID is
      //  returned.
      //
      // Notes:
      //  FIXME: We allow multiple basic blocks to have the same ID. How???
      //
      BasicBlock * getBlock (unsigned id) const {
        std::map<unsigned, BasicBlock *>::const_iterator i = BBMap.find (id);
        if (i != BBMap.end())
          return i->second;
        return 0;
      }

    protected:
      // Maps a basic block to the number to which it was assigned.  Note that
      // *multiple* basic blocks can be assigned the same ID (e.g., if a
      // transform clones a function).
      std::map<BasicBlock*, unsigned> IDMap;

      // Reverse mapping of IDMap
      std::map<unsigned, BasicBlock *> BBMap;
  };

  //
  // Pass: RemoveBasicBlockNumbers
  //
  // Description:
  //  This pass removes the metadata that numbers basic blocks.  This is
  //  necessary because the bitcode writer pass can't handle writing out basic
  //  block values.
  //
  class RemoveBasicBlockNumbers : public ModulePass {
    public:
      static char ID;
      RemoveBasicBlockNumbers () : ModulePass (ID) {}
      virtual bool runOnModule (Module & M);
      virtual void getAnalysisUsage(AnalysisUsage &AU) const {
        AU.setPreservesCFG();
      };
  };

}

#endif
