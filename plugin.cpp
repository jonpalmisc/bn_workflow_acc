//===-- Arm Conditional Cleanup Workflow (Proof-of-Concept) ---------------===//
//
// This is a tiny workflow to clean up verbose compare + branch patterns in Arm
// code, e.g. `cset` followed by `tbnz`.
//
// This is a proof of concept and is only meant to serve as an example; it has
// had basically zero testing and is certainly not fit for real-world use.
//
//===----------------------------------------------------------------------===//
//
// Copyright (c) 2023 Jon Palmisciano. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from this
//    software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
//===----------------------------------------------------------------------===//

#include <binaryninjaapi.h>
#include <lowlevelilinstruction.h>

using namespace BinaryNinja;

constexpr auto WORKFLOW_ACC = "plugin.acc.function.acc";
constexpr auto ACTIVITY_ACC_CLEANUP = "plugin.acc.function.cleanup";

using namespace BinaryNinja;

static bool TryRewriteCSEL(Ref<LowLevelILFunction> llil, size_t index) {
  auto insn = llil->GetInstruction(index);
  if (insn.operation != LLIL_IF)
    return false;

  // Both branch destinations should be two-instruction blocks that only set a
  // register (to one or zero) to be used with a branch instruction.
  auto trueBlock = llil->GetBasicBlockForInstruction(insn.GetTrueTarget());
  auto falseBlock = llil->GetBasicBlockForInstruction(insn.GetFalseTarget());
  if (trueBlock->GetLength() != 2 || falseBlock->GetLength() != 2)
    return false;

  // Check that a register is set, as mentioned above.
  auto trueBlockInsn = llil->GetInstruction(trueBlock->GetStart());
  auto falseBlockInsn = llil->GetInstruction(falseBlock->GetStart());
  if (trueBlockInsn.operation != LLIL_SET_REG ||
      falseBlockInsn.operation != LLIL_SET_REG)
    return false;

  // Ensure both blocks set the same register.
  if (trueBlockInsn.operands[0] != falseBlockInsn.operands[0])
    return false;

  // Both the true and false block need to branch to exactly one block in IL,
  // which must be the same block.
  auto trueEdges = trueBlock->GetOutgoingEdges();
  auto falseEdges = falseBlock->GetOutgoingEdges();
  if (trueEdges.size() != 1 || falseEdges.size() != 1 ||
      trueEdges[0].target != falseEdges[0].target)
    return false;

  // The shared destination must be only one instruction long and must be
  // another LLIL_IF instruction.
  auto realBlock = trueEdges[0].target;
  auto realBlockInsn = llil->GetInstruction(realBlock->GetStart());
  if (realBlock->GetLength() != 1 || realBlockInsn.operation != LLIL_IF)
    return false;

  // TODO: Check that the register used in the conditional expression is the
  // one referenced above... left as an exercise to the reader.

  auto GetLabelForIndex = [](size_t index) {
    BNLowLevelILLabel label;
    label.resolved = true;
    label.ref = 0;
    label.operand = index;

    return label;
  };

  auto realTrueLabel = GetLabelForIndex(realBlockInsn.GetTrueTarget());
  auto realFalseLabel = GetLabelForIndex(realBlockInsn.GetFalseTarget());

  // Replace the current instruction with a new LLIL_IF instruction which
  // branches to the appropriate destinations based on the original condition.
  insn.Replace(llil->If(insn.GetConditionExpr().GetNonSSAExprIndex(),
                        realTrueLabel, realFalseLabel));
  return true;
}

static void Run(Ref<AnalysisContext> context) {
  const auto func = context->GetFunction();
  const auto arch = func->GetArchitecture();

  const auto llil = context->GetLowLevelILFunction();
  if (!llil) {
    LogWarn("Failed to get LLIL for function at 0x%llx.", func->GetStart());
    return;
  }

  bool changed = false;
  for (auto const &block : llil->GetBasicBlocks())
    for (size_t i = block->GetStart(), end = block->GetEnd(); i < end; ++i)
      changed |= TryRewriteCSEL(llil, i);

  if (changed) {
    llil->GenerateSSAForm();
    llil->Finalize();
  }
}

constexpr auto WORKFLOW_INFO = R"({
  "title": "Arm Conditional Cleanup",
  "description": "",
  "capabilities": []
})";

extern "C" BINARYNINJAPLUGIN bool CorePluginInit() {
  const auto workflow = Workflow::Instance()->Clone(WORKFLOW_ACC);
  workflow->RegisterActivity(new Activity(ACTIVITY_ACC_CLEANUP, &Run));
  workflow->Insert("core.function.translateTailCalls", ACTIVITY_ACC_CLEANUP);

  BinaryNinja::Workflow::RegisterWorkflow(workflow, WORKFLOW_INFO);

  return true;
}

extern "C" BINARYNINJAPLUGIN void CorePluginDependencies() {
  AddRequiredPluginDependency("arch_arm64");
}

BN_DECLARE_CORE_ABI_VERSION
