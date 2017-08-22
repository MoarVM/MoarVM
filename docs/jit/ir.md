# Expression 'Tree' Intermediate Representation

The 'expression tree' IR has been developed developed to support
low-level optimization and advanced code generation. This document
describes this representation, the way it is generated, and the way it
is consumed. You may need this document in order to develop
specialised JIT support for a graph, add support for newly developed
VM opcodes, or to help in debugging.


## Template Syntax

Expression trees are built from the MoarVM bytecode using tree
templates, which are defined in a textual format. This textual format
has been designed for (implementation) simplicity rather than for
convenience. It's not very difficult, but it is very rigid - mistakes
are not tolerated. These templates are defined in a expression list
file. This file is then compiled to a C header file that supplies the
expression tree builder with templates The expression template
compiler is located in 'tools/expr-template-compiler.pl' and can be
invoked as such:

    perl -Itools/ tools/expr-template-compiler.pl \
        -o output-header-file.h input-file.expr

Note that expression list elements are always translated to (constant)
expressions in a C array and are subject to the limits of such
constant expressions.

A single *list* opens with a '(' and closes with ')'. Between the
parentheses there can be words, numbers, and nested lists. A
syntactically correct (but meaningless)n list is:

    (foo 32 (bar baz))

A template definition is a list that starts with the keyword
*template:* followed by the *opcode name* for which the template is
defined, and finally the *template list*. An especially simple
template that yields constants NULL pointer is:

    (template: null_s (const 0 ptr_sz))

A template list consists of *node name*, zero or more *child nodes*,
and zero or more *arguments*. The const node above has zero child
nodes and two arguments, first the constant value and second the value
size. A simple example of a nested template list would be inc_i:

    (template: inc_i (add $0 (const 1 int_sz)))

**Words** are defined in the usual way (alphanumeric characters
  interleaved with underscores), except that they may have *sigils*
  attached to them, modifying their meaning. Words without sigils
  attached are rendered uppercased and prefixed with
  **'MVM\_JIT\_'**. Thus, const is rendered as **MVM\_JIT\_CONST**,
  ptr_sz as **MVM\_JIT\_PTR\_SZ**, etc. These names should refer to
  constants declared at compile time. A node name must be an
  unmodified word.

**Substitutions** are indicated by a '$' prefix. In the inc\_i
  template, '$0' is a substition refering to the first operand of the
  inc\_i VM opcode. The substitution '$1' refers to the second
  operand, if any, '$2' to the third etc. Operand trees are
  constructed and linked into the template during expression tree
  construction.

It is possible to create your own substitutions using the *let:*
statement:

    (template: sp_p6oget_o
      (let:
         (($val (load (add (^p6obody $1) $2) ptr_sz))
         (if (nz $val) $val (vmnull))))

The first subtree of the 'let:' statement is the *definition list*. A
single *definition* consists of the *substitution name* and the
template list that it defines. Definition lists are evaluated in
left-to-right order, meaning that textually later definitions can
refer to earlier definition ames. An important side effect of 'let:'
is that *let: guarantees the evaluation order of defintions*. That is,
the tree node represented by '$val' is computed before its reference
in the 'if' expression following it. (This is not the case for operand
nodes). Finally, note that 'let:' statements declare definitions in a
single expression-wide scope and that redefinitions are not allowed.


**Statement Macro's** are lists of which the node name start with an
  ampersand '&'. These lists are compiled to C-macro
  invocations. Arguments to the macro (the other elements in the list)
  are reproduced verbatim, and nested lists are NOT supported. For
  example:

    (&MACRO_NAME foo bar 3)

  Is translated to:

    MACRO_NAME(foo, bar, 3)

  The most common use of this is actually to use the sizeof() and
  offsetof() expressions in field access, but it can also be used
  to generate a constant pointer or a message string, provided you use
  the appropriate C macro hackery.

**Tree Macro's** are a facility implemented by the template
  preprocessor to aid in developing templates. Tree macro's are
  indicated by a '^' prefix. For instance, the '(^p6obody $1)' list
  above is the invocation of a tree macro. A tree macro is substituted
  directly into the invoking expression while the template is being
  compiled. They differ from substitutions in two respects:

* substitutions are local (only valid within the scope of a let:
  expression), while macro's are globally defined. Thus, macro's can
  be used as a building block for templates, and substitutions can
  only be used within a template.

* substitutions are *linked* into the resulting tree, thus two
  references of the same substitution refer to the same IR tree
  nodes. Tree macro's are *spliced* into the tree, so two
  invocations refer to two different trees. (These trees may be
  resolved to be equal when using common subexpression evaluation, but
  that's a separate issue).

A tree macro is defined using the 'macro:' keyword, followed by the
macro name (including the '^' prefix), a list of macro arguments, and
the actual macro list. For example, the '^p6obody' macro is
implemented as:

    (macro: ^spesh_slot (,a)
       (idx
          (load (addr (frame) (&offsetof MVMFrame effective_spesh_slots)))
           ,a ptr_sz))

For visual clarity, macro arguments are indicated by a ',' prefix,
like ,a. The arguments given to the macro (words, operands, numbers,
substitution names, or entire trees) are spliced directly into the
macro. In short, *tree macro's are a syntax-level facility*, and you
should expect no more of them.

Finally, **comments** are lines starting with the '#' sign. Space or
text before this sign is not allowed.

## Stores

MoarVM opcodes typically imply the storage of a computed value into a
VM-level register. In most cases, this store operation is implicit
into the template, and the expression tree builder inserts these
stores as needed. For some opcodes, this does not work, because (for
example) their VM-level implementation does not directly yield a
value. An example would be the 'atpos' group of opcodes, which
store their result directly into VM local memory:

    (template: atpos_o!
       (let: (($addr (copy $0)))
         (if (^is_type_obj $1)
            (store $addr (vmnull) ptr_sz)
            (call (load (addr (^repr $1) (&offsetof MVMREPROps pos_funcs.at_pos)) ptr_sz)
               (arglist 7
                 (carg (tc) ptr)
                 (carg (^stable $1) ptr)
                 (carg $1 ptr)
                 (carg (^body $1) ptr)
                 (carg $2 int)
                 (carg $addr ptr)
                 (carg (const (&QUOTE MVM_reg_obj_sz) int))
               void))))

The $addr value refers to the address of output node. The template is
marked with a '!' postfix to signal that the expression yields no
value and takes care of storing the result itself. Because it yields
no value, it cannot be used as an intermediate result in the
expression. (Because it also emits a function call, it also
invalidates the current register context, but that is another
problem).

# Tree Structure

As noted above, the expression tree consists of *nodes*, which have
*children* and *arguments*. Children must always be references to
other nodes. Arguments on the other hand are intepreted directly as
integers. The file 'src/jit/expr.h' defines all node types supported
by the JIT compiler.

Some node types take a variable number of children, which is indicated
by a negative number in their node definition. For example, the
**ALL** node type may take any number of expressions that yield a
truth value (a **FLAG** value in JIT nomenclature), and yields true
only when all of them do (much like C-style boolean '&&'). The *first
chld* of any variadic node must be a constant number indicating the
number of children. As a rule, variadic nodes do not take arguments,
although that rule is not really strictly enforced. Some other
variadic nodes are **DO**, **ANY** and **ARGLIST**.

## Roots

The compilation order of the expression tree is determined by the tree
*roots*. Roots are simply ordered indices into the tree that
correspond (in principle) to the order of instructions in MoarVM
bytecode, although we reserve the right to reorder them in a
consistent way. (It is by adding roots that the 'let:' statement
ensures an evaluation order).

## Traversal

The Expression Tree IR exposes a mechanism using for preorder, inorder
and postorder traversal. This is achieved by defining a
MVMJitTreeTraverser structure and starting
MVM\_jit\_expr\_tree\_traverse().

    struct MVMJitTreeTraverser {
       void  (*preorder)(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                         MVMJitExprTree *tree, MVMint32 node);
       void   (*inorder)(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                         MVMJitExprTree *tree, MVMint32 node, MVMint32 child);
       void (*postorder)(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                         MVMJitExprTree *tree, MVMint32 node);
       void       *data;
        MVMint32 *visits;
    };


The data pointer is supposed to point to a user-supplied data
structure. The visits array is filled with the number of times a
certain node is visited. (Because the tree is really a DAG, a node can
be visited many times). It is suggested you use this to decide whether
to reevaluate your code. In most cases, I find that you don't want
revisiting behaviour.

## Node information

The default tree structure does not supply much information beyond the
structure of the graph. Additional knowledge is encoded in the
tree->info array, which is populated during various passes
through the code. I reserve the right to add and remove fields at
will, but some significant fields are:

* **value** - the MVMJitExprValue structure that holds information
  related to the value of this node (i.e. what register it is compiled
  too, the size of the value, or the memory address it refers too).

* **tile** - the MVMJitTile* that defines how this value will be
  compiled to machine code.

* **op_info** - Static information on this JIT node, e.g. the number
  of args and number of children.

* **spesh_ins** - The MVMSpeshIns* of which this value is the root,
  which is probably useful information for optimization, because it
  can include type and usage information.


## Value types

The JIT IR defines a non-strict and overlapping set of value types,
most importantly REG, MEM, LBL, FLAG, and VOID - the primary tile
result types - and INT, NUM, and PTR which are mostly useful to
indicate C function arguments. It is quite essential that C function
arguments are given their correct class to ensure that they are placed
in the correct registers.
