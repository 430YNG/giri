//===- BasicBlockNumbering.cpp - Provide BB identifiers ---------*- C++ -*-===//
//
//                    Giri: Dynamic Slicing in LLVM
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a pass that assigns a unique ID to each basic block.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "BasicBlockNumbering"

#include "Utility/BasicBlockNumbering.h"

#include "llvm/Support/Debug.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"

#include <iostream>
#include <vector>

char dg::BasicBlockNumberPass::ID    = 0;
char dg::QueryBasicBlockNumbers::ID  = 0;
char dg::RemoveBasicBlockNumbers::ID = 0;

static const char * mdKindName = "dg";

static RegisterPass<dg::BasicBlockNumberPass>
X ("bbnum", "Assign Unique Identifiers to Basic Blocks");

static RegisterPass<dg::QueryBasicBlockNumbers>
Y ("query-bbnum", "Query Unique Identifiers of Basic Blocks");

static RegisterPass<dg::RemoveBasicBlockNumbers>
Z ("remove-bbnum", "Remove Unique Identifiers of Basic Blocks");

MDNode* dg::BasicBlockNumberPass::assignIDToBlock (BasicBlock * BB, unsigned id) {
  // Fetch the context in which the enclosing module was defined.  We'll need
  // it for creating practically everything.
  LLVMContext & Context = BB->getParent()->getParent()->getContext();

  // Create a new metadata node that contains the ID as a constant.
  Value *ID[2];
  ID[0] = BB;
  ID[1] = ConstantInt::get(Type::getInt32Ty(Context), id);
  MDNode *MD = MDNode::getWhenValsUnresolved(Context, ArrayRef<Value *>(ID, 2), false);

  return MD;
}

bool dg::BasicBlockNumberPass::runOnModule (Module & M) {
  // Now create a named metadata node that links all of this metadata together.
  NamedMDNode * MD = M.getOrInsertNamedMetadata(mdKindName);
  
  // Scan through the module and assign a unique, positive (i.e., non-zero) ID
  // to every basic block.
  unsigned count = 0;
  for (Module::iterator MI = M.begin(), ME = M.end(); MI != ME; ++MI)
    for (Function::iterator BB = MI->begin(), BE = MI->end(); BB != BE; ++BB) {
      MD->addOperand((assignIDToBlock(BB, ++count)));
    }
  
  std::cout << "Total Number of Basic Blocks: " << count << std::endl;

  /* // addElement is inefficient due to bad implementation, use only Create in stead
  NamedMDNode * MD = NamedMDNode::Create (M.getContext(), name, 0, 0, &M);  
  for (unsigned index = 0; index < MDNodes.size(); ++index) 
     MD->addElement (MDNodes[index]);
  */

  //std::cout<< " Size of Metadata: "  << MDNodes.size() << std::endl;
  /* // Copy vector to array and then use it
  MDNode **MDNodeArray;
  MDNodeArray = (MDNode **) malloc ( sizeof(MDNode *) * MDNodes.size());
  for (unsigned index = 0; index < MDNodes.size(); ++index) 
  MDNodeArray[index] = MDNodes[index];  
  */

  // If there is any memory error, use copying in place of using address of first element of vector
  // NamedMDNode * MD = NamedMDNode::Create (M.getContext(), name, /*(MetadataBase*const*) MDNodeArray*/  (MetadataBase*const*) &MDNodes[0] , MDNodes.size(), &M);
 
  // We always modify the module.
  return true;
}

bool dg::QueryBasicBlockNumbers::runOnModule (Module & M) {
  //std::cout << "Inside QueryBasicBlockNumbers " << M.getModuleIdentifier() << std::endl;
  //
  // Get the basic block metadata.  If there isn't any metadata, then no basic
  // block has been numbered.
  //
  const NamedMDNode * MD = M.getNamedMetadata (mdKindName);
  if (!MD) return false;

  //
  // Scan through all of the metadata (should be pairs of basic blocks/IDs) and
  // bring them into our internal data structure.
  //
  for (unsigned index = 0; index < MD->getNumOperands(); ++index) {
    //
    // The basic block should be the first element, and the ID should be the
    // second element.
    //
    MDNode * Node = dyn_cast<MDNode>(MD->getOperand (index));
    assert (Node && "Wrong type of meta data!\n");
    BasicBlock * BB = dyn_cast<BasicBlock>(Node->getOperand (0));
    ConstantInt * ID = dyn_cast<ConstantInt>(Node->getOperand (1));

    //
    // Do some assertions to make sure that everything is sane.
    //
    assert (BB && "MDNode first element is not a BasicBlock!\n");
    assert (ID && "MDNode second element is not a ConstantInt!\n");

    //
    // Add the values into the map.
    //
    assert (ID->getZExtValue() && "BB with zero ID!\n");
    IDMap[BB] = ID->getZExtValue();
    unsigned id = (unsigned) ID->getZExtValue();
    bool inserted = BBMap.insert (std::make_pair(id,BB)).second;
    assert (inserted && "Repeated identifier!\n");
  }

  return false;
}

bool dg::RemoveBasicBlockNumbers::runOnModule (Module & M) {
  // Get the basic block metadata.  If there isn't any metadata, then no basic
  // blocks have been numbered.
  NamedMDNode * MD = M.getNamedMetadata (mdKindName);
  if (!MD) return false;

  // Remove the metadata.
  MD->eraseFromParent();

  // Assume we always modify the module.
  return true;
}
