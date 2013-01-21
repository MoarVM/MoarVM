# MAST Binary Serialization
Code to execute is provided to MoarVM in the form of an AST, which has been
packed into a binary form. That form is described in this file. Note that this
has been designed to be easy for a JIT compiler to work with or for turning in
to some other easy to execute form rather than for rapid direct execution; a
fast interpreter will pre-deref various things. That can be done at the same
time the MAST is being verified for correctness, which can be done lazily on
the first execution.

## Alignment
Each node is stored at a 16-bit boundary.

## Compilation Units


## Frames


## Node Encoding
An initial byte determines the type of node. What follows is then node
dependent. Here's a summary.

    0   No node, ignore
    1   Statements node, with < 256 children
    2   Statements node, with < 16777216 children
    3   Call node
    4   Method call node
    5   String literal node, heap index < 256
    6   String literal node, heap index < 16777216
    7   Integer literal node, 8-bit
    8   Integer literal node, 16-bit signed
    9   Integer literal node, 32-bit signed
    10  Integer literal node, 64-bit signed
    11  Floating point literal node, 32-bit
    12  Floating point literal node, 64-bit
    13  if node with "then" section only
    14  if node with "else" section only (aka unless)
    15  if node with then and else sections
    16  while node, pre-test
    17  while node, post-test
    18  while node, pre-test, negate
    19  while node, post-test, negate
    20  jump table, < 256 cases
    21  jump table, < 16777216 cases
    22  SC lookup; SC selector < 256, SC index < 65536
    23  SC lookup; SC selector < 16777216, 32-bit SC index
    128 Built-in opcode, bank 1
    129 Built-in opcode, bank 2
    ...
    254 Non-core op
    255 Reserved

### No node
This just causes this byte to be skipped over and the next one to be read. It
could be used for padding.

### Statements nodes
Immediately after the node, either one or three bytes specify the number
of child nodes that we can expect to find that are owned by this node.

### Call node
Indicates a call.

* The three bytes that immediately follow indicate the call site descriptor
* Following this are a number of nodes as indicated by the descriptor, which
  correspond to the computation of the arguments to be passed
* After this comes a node that specifies how to look up the thing to call

### Method call node
Indicates a call to a method.

* The three bytes that immediately follow indicate the call site descriptor
* Following this are a number of nodes as indicated by the descriptor, which
  correspond to the computation of the arguments to be passed. The invocant
  - that is, the object to make the call on - is the first of these.
* After this comes a node that computes the name of the method to call (for
  compile-time known calls, this will just be a string literal node)

### String literals
Immediately after this node, either one or three bytes specify the index into
the string heap.

### Integer and float literals
The type of literal indicates what to read next. If it's an 8-bit literal, we
read the next byte and are done. Otherwise, we skip a byte and then read the
value, to increase chance of an aligned read (since we don't execute the AST
directly, though, it doesn't matter terribly much if the one time we do read
it from here is a little slow).

### If nodes


### While nodes


### Switch nodes


## Callsites
A callsite identifies a set of types of arguments being passed, along with
per-argument flags. It is serialized as follows.

* 16 bits to indicate the number of arguments
* A byte array of flags, one byte per argument
* Alignment space so next read is at 32 bit boundary
* An array of 32-bit representation IDs, giving the representation of
  each argument, or 0 if there is none known (which means we have a
  reference type for sure)
  
The lower two bits indicate the kind of primitive type we have.

    0   Reference type
    1   Native integer
    2   Native number
    3   Native string
    
The rest are bit flags.

    4   This is the name of the following parameter (thus a named param)
    8   The argument should be flattened (up to the receiver to make this
        mean something)
        
The remaining flags are reserved.
