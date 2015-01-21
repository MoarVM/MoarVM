# 6model parametric extensions in MoarVM

## Overview

The 6model parametric extensions add parametric type support at the VM level.
A type may configure itself as supporting parameterization. Parameterizations
each have a unique key, which is used to intern them. This ensures each of the
parameterizations exists only once. In the case that two modules both produce
and serialize a parameterization, that from the second module may be freely
disregarded, and the existing deserialization of the parameterized type may
be used. This ensures unique type objects per parameterization are upheld even
in the case of precompilation.

Each parameterization will have a unique STable and type object. It is up to
the meta-object whether the HOW is shared between parameterizations. For
example, parametric roles reify the methods within them with concrete type
parameters, so a separate HOW is required. By contrast, CoerceHOW, the
meta-object for coercion types, can store all that is distinctive about it
within the type's parameters. Since a given parameteriation can be queried
for its parameters, it is possible for all Perl 6 coercion types to share
a single meta-object.

## STable extensions

The mode flags on an STable get two new additions:

* Parametric (this type can be parameterized)
* Parameterization (this type is the parameterization of some parametric type)

A type cannot be both parametric and a parameterization, meaning we can use a
union to store parametrics-related data.

    union {
        struct {
            MVMObject *parameterizer;           /* Thing to invoke)
            MVMParameterizationLookup *lookup;  /* Known parameterizations */
        } parametric;
        struct {
            MVMObject *parametric_type;         /* The parameterized type */
            MVMObject *parameters;              /* The parameters. */
        } parameterized;
    }

## New ops

The VM op additions match the nqp::op additions for parametric types:

### nqp::setparameterizer(type, parameterizer)

Makes a type as being parametric, and configures the code needed to parameterize
it.

### nqp::parameterizetype(type, parameter_array)

Takes a parameterizable type and an array of parameters. Looks up and returns
any existing matching parameterization. If it does not exist, invokes the
parameterization producer for the parametric type, installs that in the lookup,
and returns it. Note that the array is snapshotted, for the benefits of the
type specializer.

### nqp::typeparameterized(type)

If the type specified is a parameterization of some other type, then returns that
type. Otherwise, returns null.

### nqp::typeparameters(type)

Gets the type parameters used to parameterize the type. Throws an exception if the
type is not parametric.

### nqp::typeparameterat(type, idx)

Equivalent to nqp::atpos(nqp::typeparameters(type), idx), except much easier for
type specialization to understand and deal with and avoids an array construction.

## Interaction with spesh

Specializations can take place per parameterization of a type. This means that uses
of nqp::typeparameterat(...) can become constants at specialization time, which opens
up numerous optimization possibilities.

## Deserialize-time interning

XXX To define
