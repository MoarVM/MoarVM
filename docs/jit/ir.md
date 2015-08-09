# Expression 'Tree' Intermediate Representation

The 'expression tree' IR has been developed developed to support
low-level optimization and advanced code generation. This document
describes this representation, the way it is generated, and the way it
is consumed. You may need this document in order to develop
specialised JIT support for a graph, add support for newly developed
VM opcodes, or to help in debugging.


## Template Syntax

In most cases, the expression tree is build from templates. These
templates are defined according to a textual format, and compiled to a
C array that can be accessed at runtime. The textual format has been
designed for (implementation) simplicity rather than for
convenience. Nevertheless, it's not very difficult. A single *list*
opens with a '(' and closes with ')'. Between the parentheses there
can be words, numbers, and nested lists.

A template definition starts with the keyword *template:* followed by
the *opcode name* for which the template is defined, and finally the
*template list*. An especially simple template that yields constants
NULL pointer is:

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
is that *let guarantees the evaluation order of defintions*. That is,
the tree node represented by '$val' is computed before its reference
in the 'if' expression following it. (This is not the case for operand
nodes).
