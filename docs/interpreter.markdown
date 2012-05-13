# MoarVM Intepreter

## Ops and Op Banks
The interpreter first dispatches on op bank, then on the code within that.
For the core and primitive operations, that is done through a switch that
is inlined directly inside of the interpreter.

The list of ops is held in src/core/oplist. This is processed by a the
tools/update_pos_h.p6 tool to generate src/core/ops.h, which contains all
of the metadata about the operations and operation banks.

## Nested Runloops - Just Say No
There is no notion of "nested runloop"; any call into C land that wants to
call back into the interpreter must persist enough information to allow it
to continue its work later. It does this by saving that info into a frame
and specifying a callback to resume the work. In essence, it needs to be
written out as a state machine. That state machine will be called back into
when a C frame is returned to. This is not particularly fun. Nested runloops
and continuation barrier issues are even less fun, though.
