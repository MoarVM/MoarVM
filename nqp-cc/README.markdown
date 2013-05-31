# NQP Cross-Compiler

NQP already runs on Parrot. The cross-compiler enables writing tests in
NQP that build up MAST trees (or even QAST ones that are translated into
MAST) and having them persisted as MoarVM bytecode files. This can then
be run on MoarVM.

Eventually MoarVM will be able to run NQP itself; until then, this is the
way to test the VM.
