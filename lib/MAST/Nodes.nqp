# MoarVM AST nodes
# ================
# The set of nodes that may appear in a MAST (MoarVM AST) tree.
# This is the low level AST for the VM; we persist this tree in
# an encoded binary form, and it is used to interpret, JIT from
# and so forth.

# The base class for all nodes.
class MAST::Node {
}

# Everything lives within a compilation unit. Note that this may
# or may not map to a HLL notion of compilation unit; it is always
# a set of things that we're going to compile "in one go".
class MAST::CompUnit is MAST::Node {
    # Array of REPR types we depend on, with the ID of each
    # one being the array index.
    has @!REPRs;
    
    # Array of serialization contexts we depend on, with the ID
    # of each one being the array index.
    has @!SCs;
    
    # The set of frames that make up this compilation unit.
    has @!frames;
    
    # Has a set of static callsite descriptors, which describe the
    # types passed for a given call site.
    has @!callsites;
}

# Represents a frame, which is a unit of invocation. This captures the
# static aspects of a frame.
class MAST::Frame is MAST::Node {
    # The set of lexicals that we allocate space for and keep until
    # nothing references an "instance" of the frame. This is the
    # list of lexical types, the index being significant. Any type
    # that has a flatteing representation will be "flattened" in to
    # the frame itself.
    has @!lexical_types;
    
    # Mapping of lexical names to slot indexes.
    has %!lexical_names;

    # The set of locals we allocate, but don't need once the frame
    # has finished executing. This is the set of types. Note that
    # they do not get a name.
    has @!local_types;
    
    method add_lexical($type, $name) {
        my $index := +@!lexical_types;
        @!lexical_types[$index] := $type;
        %!lexical_names{$name} := $index;
        $index
    }
    
    method lexical_index($name) {
        pir::exists(%!lexical_names, $name) ??
            %!lexical_names{$name} !!
            pir::die("No such lexical '$name'")
    }
    
    method add_local($type) {
        my $index := +@!local_types;
        @!local_types[$index] := $type;
        $index
    }
}

# A sequence of instructions. Evaluates to the result of the last
# statement.
class MAST::Stmts is MAST::Node {
    has @!stmts;
}

# An operation to be executed. Includes the operation code and the
# argument count.
class MAST::Op is MAST::Node {
    has $!op;
    has @!operands;
}

# A call. The first child is the thing that is to be called, and the
# rest represent the arguments.
class MAST::Call is MAST::Node {
    has $!lookup;
    has @!arg_flags;
    has @!args;
}

# A method call. The arguments will be computed, and the first used
# as the object to make the call on. The name is 
class MAST::CallMethod is MAST::Node {
    has $!name;
    has @!arg_flags;
    has @!args;
}

# Literal values.
class MAST::StrLit is MAST::Node {
    # The string value.
    has str $!value;
}
class MAST::IntLit is MAST::Node {
    # The integer value.
    has int $!value;
    
    # Size in bits (8, 16, 32, 64).
    has int $!size;
    
    # Whether or not it's signed.
    has int $!signed;
}
class MAST::NumLit is MAST::Node {
    # The floating point value.
    has num $!value;
    
    # Size in bits (32, 64).
    has int $!size;
}

# Executes the condition code. If it's true, runs the "then" code if any.
# If it's false, runs the "else" code if any. Note that this can serve
# as both an "if", an "unless" and an "if/else", since either or both of
# the "then" and "else" can be provided.
class MAST::If is MAST::Node {
    has $!condition;
    has $!then;
    has $!else;
}

# Represents a while loop. Keeps executing while the condition is true,
# or if "negate" is set while the condition is false. If the "postcheck"
# flag is set then the body is always done once and the check comes at
# the end.
class MAST::While is MAST::Node {
    has $!condition;
    has $!body;
    has int $!negate;
    has int $!postcheck;
}

# Represents a jump table.
class MAST::JumpTable is MAST::Node {
    # The instructions to execute in order to get the place to jump to.
    has $!index_source;
    
    # Mapping of indexes to the thing to do for each index.
    has %!index_to_ops;
}
