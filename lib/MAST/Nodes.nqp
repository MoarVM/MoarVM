use MASTOps;

# MoarVM AST nodes
# This file contains a set of nodes that are compiled into MoarVM
# bytecode. These nodes constitute the official high-level interface
# to the VM. At some point, the bytecode itself will be declared
# official also. Note that no text-based mapping to/from these nodes
# will ever be official, however.

# The base class for all nodes.
class MAST::Node {
}

# Everything lives within a compilation unit. Note that this may
# or may not map to a HLL notion of compilation unit; it is always
# a set of things that we're going to compile "in one go". The
# input to the AST to bytecode convertor should always be one of
# these.
class MAST::CompUnit is MAST::Node {
    # Array of REPR types we depend on, with the ID of each
    # one being the array index.
    has @!REPRs;
    
    # Array of serialization contexts we depend on, with the ID
    # of each one being the array index.
    has @!SCs;
    
    # The set of frames that make up this compilation unit.
    has @!frames;
    
    method new() {
        nqp::create(self)
    }
    
    method add_frame($frame) {
        @!frames[+@!frames] := $frame;
    }
}

# Represents a frame, which is a unit of invocation. This captures the
# static aspects of a frame.
class MAST::Frame is MAST::Node {
    # A compilation-unit unique identifier for the frame.
    has str $!cuuid;
    
    # A name (need not be unique) for the frame.
    has str $!name;
    
    # The set of lexicals that we allocate space for and keep until
    # nothing references an "instance" of the frame. This is the
    # list of lexical types, the index being significant. Any type
    # that has a flattening representation will be "flattened" in to
    # the frame itself.
    has @!lexical_types;
    
    # Mapping of lexical names to slot indexes.
    has @!lexical_names;

    # The set of locals we allocate, but don't need once the frame
    # has finished executing. This is the set of types. Note that
    # they do not get a name.
    has @!local_types;
    
    # The instructions for this frame.
    has @!instructions;
    
    # The outer frame, if any.
    has $!outer;
    
    # Mapping of lexical names to lexical index, for lookups.
    has %!lexical_map;
    
    my $cuuid_src := 0;
    sub fresh_id() {
        $cuuid_src := $cuuid_src + 1;
        "!MVM_CUUID_$cuuid_src"
    }
    
    method new(:$cuuid = fresh_id(), :$name = '<anon>') {
        my $obj := nqp::create(self);
        $obj.BUILD($cuuid, $name);
        $obj
    }
    
    method BUILD($cuuid, $name) {
        $!cuuid         := $cuuid;
        $!name          := $name;
        @!lexical_types := nqp::list();
        @!lexical_names := nqp::list();
        @!local_types   := nqp::list();
        @!instructions  := nqp::list();
        $!outer         := MAST::Node;
        %!lexical_map   := nqp::hash();
    }
    
    method add_lexical($type, $name) {
        my $index := +@!lexical_types;
        @!lexical_types[$index] := $type;
        @!lexical_names[$index] := $name;
        %!lexical_map{$name} := $index;
        $index
    }
    
    method lexical_index($name) {
        pir::exists(%!lexical_map, $name) ??
            %!lexical_map{$name} !!
            pir::die("No such lexical '$name'")
    }
    
    method add_local($type) {
        my $index := +@!local_types;
        @!local_types[$index] := $type;
        $index
    }
    
    method instructions() {
        @!instructions
    }
    
    method set_outer($outer) {
        if nqp::istype($outer, MAST::Frame) {
            $!outer := $outer;
        }
        else {
            nqp::die("set_outer expects a MAST::Frame");
        }
    }
}

# An operation to be executed. Includes the operation code and the
# operation bank it comes from. The operands must be either registers,
# literals or labels (depending on what the instruction needs).
class MAST::Op is MAST::Node {
    has int $!bank;
    has int $!op;
    has @!operands;
    
    method new(:$bank!, :$op!, *@operands) {
        my $obj := nqp::create(self);
        for @operands {
            nqp::die("Operand not a MAST::Node") unless $_ ~~ MAST::Node;
        }
        unless nqp::existskey(MAST::Ops.WHO, '$' ~ $bank) {
            nqp::die("Invalid MAST op bank '$bank'");
        }
        unless nqp::existskey(MAST::Ops.WHO{'$' ~ $bank}, $op) {
            nqp::die("Invalid MAST op '$op'");
        }
        nqp::bindattr_i($obj, MAST::Op, '$!bank', MAST::OpBanks.WHO{'$' ~ $bank});
        nqp::bindattr_i($obj, MAST::Op, '$!op', MAST::Ops.WHO{'$' ~ $bank}{$op});
        nqp::bindattr($obj, MAST::Op, '@!operands', @operands);
        $obj
    }
    
    method bank() { $!bank }
    method op() { $!op }
    method operands() { @!operands }
}

# Literal values.
class MAST::SVal is MAST::Node {
    has str $!value;
    
    method new(:$value!) {
        my $obj := nqp::create(self);
        nqp::bindattr_s($obj, MAST::SVal, '$!value', $value);
        $obj
    }
}
class MAST::IVal is MAST::Node {
    # The integer value.
    has int $!value;
    
    # Size in bits (8, 16, 32, 64).
    has int $!size;
    
    # Whether or not it's signed.
    has int $!signed;
    
    method new(:$value!, :$size = 64, :$signed = 1) {
        my $obj := nqp::create(self);
        nqp::bindattr_i($obj, MAST::IVal, '$!value', $value);
        nqp::bindattr_i($obj, MAST::IVal, '$!size', $size);
        nqp::bindattr_i($obj, MAST::IVal, '$!signed', $signed);
        $obj
    }
}
class MAST::NVal is MAST::Node {
    # The floating point value.
    has num $!value;
    
    # Size in bits (32, 64).
    has int $!size;
    
    method new(:$value!, :$size = 64) {
        my $obj := nqp::create(self);
        nqp::bindattr_n($obj, MAST::NVal, '$!value', $value);
        nqp::bindattr_i($obj, MAST::NVal, '$!size', $size);
        $obj
    }
}

# Labels (used directly in the instruction stream indicates where the
# label goes; can also be used as an instruction operand).
class MAST::Label is MAST::Node {
    has str $!name;
    
    method new(:$name!) {
        my $obj := nqp::create(self);
        nqp::bindattr_s($obj, MAST::Label, '$!name', $name);
        $obj
    }
    
    method name() { $!name }
}

# A local lookup.
class MAST::Local is MAST::Node {
    has int $!index;

    method new(:$index!) {
        my $obj := nqp::create(self);
        nqp::bindattr_i($obj, MAST::Local, '$!index', $index);
        $obj
    }
}

# A lexical lookup.
class MAST::Lexical is MAST::Node {
    has int $!index;
    has int $!frames_out;
    
    method new(:$index!, :$frames_out = 0) {
        my $obj := nqp::create(self);
        nqp::bindattr_i($obj, MAST::Lexical, '$!index', $index);
        nqp::bindattr_i($obj, MAST::Lexical, '$!frames_out', $frames_out);
        $obj
    }
}

# Argument flags.
module Arg {
    our $obj   := 1;
    our $int   := 2;
    our $uint  := 4;
    our $num   := 8;
    our $str   := 16;
    our $named := 32;
    our $flat  := 64;
}

# A call. A register holding the thing to call should be specified, along
# with a set of flags describing the call site, followed by the arguments
# themselves, which may be constants or come from registers. There is also
# a set of flags, describing each argument. Some flags need two actual
# arguments, one specifying the name, the next the actual value.
class MAST::Call is MAST::Node {
    has $!target;
    has @!flags;
    has @!args;
    has $!result;
    
    method new(:$target!, :@flags!, :$result = MAST::Node, *@args) {
        sanity_check(@flags, @args);
        my $obj := nqp::create(self);
        nqp::bindattr($obj, MAST::Call, '$!target', $target);
        nqp::bindattr($obj, MAST::Call, '@!flags', @flags);
        nqp::bindattr($obj, MAST::Call, '@!args', @args);
        nqp::bindattr($obj, MAST::Call, '$!result', $result);
        $obj
    }
    
    sub sanity_check(@flags, @args) {
        my $flag_needed_args := 0;
        for @flags {
            $flag_needed_args := $flag_needed_args +
                ($_ +& $Arg::named ?? 2 !! 1);
        }
        if +@args < $flag_needed_args {
            nqp::die("Flags indicated there should be $flag_needed_args args, but have " ~
                +@args);
        }
    }
}
