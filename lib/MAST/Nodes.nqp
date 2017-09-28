use MASTOps;

# MoarVM AST nodes
# This file contains a set of nodes that are compiled into MoarVM
# bytecode. These nodes constitute the official high-level interface
# to the VM. At some point, the bytecode itself will be declared
# official also. Note that no text-based mapping to/from these nodes
# will ever be official, however.

# Extension op name/signature registry; keeps track of all the known extension
# ops and their signatures.
class MAST::ExtOpRegistry {
    my %extop_sigs;

    # Registers an extension op, specifying a name and type expected types of
    # each of the operands.
    method register_extop($name, *@sig) {
        if nqp::existskey(%extop_sigs, $name) {
            nqp::die("MoarVM extension op '$name' already registered");
        }
        my @sig_i := nqp::list_i();
        for @sig {
            nqp::push_i(@sig_i, $_);
        }
        %extop_sigs{$name} := @sig_i;
    }

    # Checks if an extop is registered.
    method extop_known($name) {
        nqp::existskey(%extop_sigs, $name)
    }

    # Gets the signature of an extop, which we can rely on to be a list of
    # native integers.
    method extop_signature($name) {
        unless nqp::existskey(%extop_sigs, $name) {
            nqp::die("MoarVM extension op '$name' is not known");
        }
        %extop_sigs{$name}
    }
}

# The extension of base number (everything below is internal).
my int $EXTOP_BASE := 1024;

# The base class for all nodes.
class MAST::Node {
    method dump($indent = "") {
        my @lines := nqp::list();
        self.dump_lines(@lines, $indent);
        nqp::join("\n", @lines);
    }

    method dump_lines(@lines, $indent) {
        nqp::push(@lines, $indent~"MAST::Node <null>");
    }
}

# Everything lives within a compilation unit. Note that this may
# or may not map to a HLL notion of compilation unit; it is always
# a set of things that we're going to compile "in one go". The
# input to the AST to bytecode convertor should always be one of
# these.
class MAST::CompUnit is MAST::Node {
    # The set of frames that make up this compilation unit.
    has @!frames;

    # The HLL name.
    has str $!hll;

    # The frame for the main entry point, if any.
    has $!main_frame;

    # The frame for the library-load entry point, if any.
    has $!load_frame;

    # The frame containing the deserialization code, if any.
    has $!deserialize_frame;

    # SC handles that we depend on.
    has @!sc_handles;

    # Mapping of SC handle names to indexes, for faster lookup.
    has %!sc_lookup;

    # List of extops that we are using. For each extop used in this compunit,
    # this list contains its signature.
    has @!extop_sigs;

    # Mapping of extop names to extop signature indexes (in the @!extop_sigs
    # array).
    has %!extop_idx;

    # String list of extop names.
    has @!extop_names;

    method add_frame($frame) {
        my int $idx := nqp::elems(@!frames);
        $frame.set_index($idx);
        nqp::push(@!frames, $frame);
    }

    method dump_lines(@lines, $indent) {
        nqp::push(@lines, $_.dump($indent)) for @!frames;
    }

    method hll($hll?) {
        nqp::defined($hll)
            ?? ($!hll := $hll)
            !! $!hll
    }

    method main_frame($frame?) {
        nqp::defined($frame)
            ?? ($!main_frame := $frame)
            !! $!main_frame
    }

    method load_frame($frame?) {
        nqp::defined($frame)
            ?? ($!load_frame := $frame)
            !! $!load_frame
    }

    method deserialize_frame($frame?) {
        nqp::defined($frame)
            ?? ($!deserialize_frame := $frame)
            !! $!deserialize_frame
    }

    method sc_idx($sc) {
        my str $handle := nqp::scgethandle($sc);
        if nqp::existskey(%!sc_lookup, $handle) {
            nqp::atkey(%!sc_lookup, $handle)
        }
        else {
            my $id := nqp::elems(@!sc_handles);
            nqp::push(@!sc_handles, $handle);
            nqp::bindkey(%!sc_lookup, $handle, $id);
            $id
        }
    }

    # Gets the opcode for an extop in the current compilation unit. If this is
    # the first use of the extop, gives it an index for this compilation unit.
    method get_extop_code(str $name) {
        if nqp::existskey(%!extop_idx, $name) {
            %!extop_idx{$name} + $EXTOP_BASE
        }
        else {
            my int $idx         := +@!extop_sigs;
            @!extop_names[$idx] := $name;
            @!extop_sigs[$idx]  := MAST::ExtOpRegistry.extop_signature($name);
            %!extop_idx{$name}  := $idx;
            $idx + $EXTOP_BASE
        }
    }
}

sub get_typename($type) {
    ["obj","int","num","str"][nqp::objprimspec($type)]
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

    # Flag bits.
    my int $FRAME_FLAG_EXIT_HANDLER := 1;
    my int $FRAME_FLAG_IS_THUNK     := 2;
    my int $FRAME_FLAG_HAS_CODE_OBJ := 4;
    my int $FRAME_FLAG_HAS_INDEX    := 32768; # Can go after a rebootstrap.
    my int $FRAME_FLAG_HAS_SLV      := 65536; # Can go after a rebootstrap.
    has int $!flags;

    # The frame index in the compilation unit (cached to aid assembly).
    has int $!frame_idx;

    # Integer array with 4 entries per static lexical value:
    # - The lexical index in the frame
    # - A flag (0 = static, 1 = container var, 2 = state var)
    # - SC index in this compilation unit
    # - Index of the object within that SC
    has @!static_lex_values;

    # Code object SC dependency index and SC index.
    has int $!code_obj_sc_dep_idx;
    has int $!code_obj_sc_idx;

    my int $cuuid_src := 0;
    sub fresh_id() {
        $cuuid_src++;
        "!MVM_CUUID_$cuuid_src"
    }

    method new(:$cuuid = fresh_id(), :$name = '<anon>') {
        my $obj := nqp::create(self);
        $obj.BUILD($cuuid, $name);
        $obj
    }

    method BUILD($cuuid, $name) {
        $!cuuid             := $cuuid;
        $!name              := $name;
        @!lexical_types     := nqp::list();
        @!lexical_names     := nqp::list();
        @!local_types       := nqp::list();
        @!instructions      := nqp::list();
        $!outer             := MAST::Node;
        %!lexical_map       := nqp::hash();
        @!static_lex_values := nqp::list_i();
    }

    method set_index(int $idx) {
        $!frame_idx := $idx;
        $!flags     := nqp::bitor_i($!flags, $FRAME_FLAG_HAS_INDEX);
    }

    method add_lexical($type, $name) {
        my $index := +@!lexical_types;
        @!lexical_types[$index] := $type;
        @!lexical_names[$index] := $name;
        %!lexical_map{$name} := $index;
        $index
    }

    method lexical_index($name) {
        nqp::existskey(%!lexical_map, $name) ??
            %!lexical_map{$name} !!
            nqp::die("No such lexical '$name'")
    }

    method add_static_lex_value($index, $flags, $sc_idx, $idx) {
        my @slv := @!static_lex_values;
        nqp::push_i(@slv, $index);
        nqp::push_i(@slv, $flags);
        nqp::push_i(@slv, $sc_idx);
        nqp::push_i(@slv, $idx);
        $!flags := nqp::bitor_i($!flags, $FRAME_FLAG_HAS_SLV);
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

    method cuuid() { $!cuuid }
    method name() { $!name }

    method has_exit_handler($value = -1) {
        if $value > 0 {
            $!flags := nqp::bitor_i($!flags, $FRAME_FLAG_EXIT_HANDLER);
        }
        nqp::bitand_i($!flags, $FRAME_FLAG_EXIT_HANDLER)
    }

    method is_thunk($value = -1) {
        if $value > 0 {
            $!flags := nqp::bitor_i($!flags, $FRAME_FLAG_IS_THUNK);
        }
        nqp::bitand_i($!flags, $FRAME_FLAG_IS_THUNK)
    }

    method set_code_object_idxs(int $sc_dep_idx, int $sc_idx) {
        $!code_obj_sc_dep_idx := $sc_dep_idx;
        $!code_obj_sc_idx     := $sc_idx;
        $!flags               := nqp::bitor_i($!flags, $FRAME_FLAG_HAS_CODE_OBJ);
    }

    method dump_lines(@lines, $indent) {
        nqp::push(@lines, $indent~"MAST::Frame name<$!name>, cuuid<$!cuuid>");
        if !nqp::chars($indent) {
            my $lex;
            my $x := 0;
            my $locals := "$indent  Local types: ";
            $locals := $locals ~ $x++ ~ "<" ~ get_typename($_) ~ ">, " for @!local_types;
            nqp::push(@lines, $locals);
            if nqp::elems(@!lexical_types) {
                $x := 0;
                $lex := "$indent  Lexical types: ";
                $lex := $lex ~ $x++ ~ "<" ~ get_typename($_) ~ ">, " for @!lexical_types;
                nqp::push(@lines, $lex);
            }
            if nqp::elems(@!lexical_names) {
                $x := 0;
                $lex := "$indent  Lexical names: ";
                $lex := $lex ~ $x++ ~ "<$_>, " for @!lexical_names;
                nqp::push(@lines, $lex);
            }
            if nqp::elems(%!lexical_map) {
                $lex := "$indent  Lexical map: ";
                $lex := "$lex$_" ~ '<' ~ %!lexical_map{$_} ~ '>, ' for %!lexical_map;
                nqp::push(@lines, $lex);
            }
            nqp::push(@lines, "$indent  Outer: " ~ (
                $!outer && $!outer.cuuid ne $!cuuid
                ?? "name<" ~ $!outer.name ~ ">, cuuid<"~$!outer.cuuid ~ '>'
                !! "<none>"
            ));
            nqp::push(@lines, "$indent  Instructions:");
            $x := 0;
            for @!instructions {
                my $prefix := $indent ~ '  [' ~ $x++ ~ '] ';
                nqp::push(@lines, $prefix ~ $_.dump($indent));
            }
            nqp::push(@lines, '');
        }
    }
}

# An operation to be executed. The operands must be either registers,
# literals or labels (depending on what the instruction needs).
class MAST::Op is MAST::Node {
    has int $!op;
    has @!operands;

    my %op_codes := MAST::Ops.WHO<%codes>;
    my @op_names := MAST::Ops.WHO<@names>;

    method new(str :$op!, *@operands) {
        my $obj := nqp::create(self);
        nqp::bindattr_i($obj, MAST::Op, '$!op', %op_codes{$op});
        nqp::bindattr($obj, MAST::Op, '@!operands', @operands);
        $obj
    }

    method new_with_operand_array(@operands, str :$op!) {
        my $obj := nqp::create(self);
        nqp::bindattr_i($obj, MAST::Op, '$!op', %op_codes{$op});
        nqp::bindattr($obj, MAST::Op, '@!operands', @operands);
        $obj
    }

    method op() { $!op }
    method operands() { @!operands }

    method dump_lines(@lines, $indent) {
        my str $opname := nqp::atpos_s(@op_names, $!op);
        nqp::push(@lines, $indent ~ "MAST::Op $opname");
        nqp::push(@lines, $_.dump($indent ~ '    ')) for @!operands;
    }
}

# An extension operation to be executed. The operands must be either
# registers, literals or labels (depending on what the instruction needs).
class MAST::ExtOp is MAST::Node {
    has int $!op;
    has @!operands;
    has str $!name;

    method new(str :$op!, :$cu!, *@operands) {
        my $obj := nqp::create(self);
        nqp::bindattr_i($obj, MAST::ExtOp, '$!op', $cu.get_extop_code($op));
        nqp::bindattr($obj, MAST::ExtOp, '@!operands', @operands);
        nqp::bindattr_s($obj, MAST::ExtOp, '$!name', $op);
        $obj
    }

    method new_with_operand_array(@operands, str :$op!, :$cu!) {
        my $obj := nqp::create(self);
        nqp::bindattr_i($obj, MAST::ExtOp, '$!op', $cu.get_extop_code($op));
        nqp::bindattr($obj, MAST::ExtOp, '@!operands', @operands);
        nqp::bindattr_s($obj, MAST::ExtOp, '$!name', $op);
        $obj
    }

    method op() { $!op }
    method operands() { @!operands }

    method dump_lines(@lines, $indent) {
        nqp::push(@lines, $indent ~ "MAST::ExtOp $!name");
        nqp::push(@lines, $_.dump($indent ~ '    ')) for @!operands;
    }
}

# Literal values.
class MAST::SVal is MAST::Node {
    has str $!value;

    method new(:$value!) {
        my $obj := nqp::create(self);
        nqp::bindattr_s($obj, MAST::SVal, '$!value', $value);
        $obj
    }

    method dump_lines(@lines, $indent) {
        # XXX: escape line breaks and such...
        nqp::push(@lines, $indent~"MAST::SVal value<$!value>");
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

    method dump_lines(@lines, $indent) {
        nqp::push(@lines, $indent~"MAST::IVal value<$!value>, size<$!size>, signed<$!signed>");
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

    method dump_lines(@lines, $indent) {
        nqp::push(@lines, $indent~"MAST::NVal value<$!value>, size<$!size>");
    }
}

# Labels (used directly in the instruction stream indicates where the
# label goes; can also be used as an instruction operand).
class MAST::Label is MAST::Node {
    method new() {
        nqp::create(self)
    }

    method dump_lines(@lines, $indent) {
        my int $addr := nqp::where(self);
        nqp::push(@lines, $indent ~ "MAST::Label <$addr>");
    }
}

# A local lookup.
class MAST::Local is MAST::Node {
    has int $!index;

    method new(:$index!) {
        my $obj := nqp::create(self);
        nqp::bindattr_i($obj, MAST::Local, '$!index', $index);
        $obj
    }

    method index() { $!index }

    method dump_lines(@lines, $indent) {
        nqp::push(@lines, $indent~"MAST::Local index<$!index>");
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

    method index() { $!index }

    method dump_lines(@lines, $indent) {
        nqp::push(@lines, $indent~"MAST::Lexical index<$!index>, frames_out<$!frames_out>");
    }
}

# Argument flags.
module Arg {
    our $obj   := 1;
    our $int   := 2;
    our $num   := 4;
    our $str   := 8;
    our $named := 32;
    our $flat  := 64;
    our $flatnamed := 128;
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
    has int $!op;

    method new(:$target!, :@flags!, :$result = MAST::Node, :$op = 0, *@args) {
        sanity_check(@flags, @args);
        my $obj := nqp::create(self);
        nqp::bindattr($obj, MAST::Call, '$!target', $target);
        nqp::bindattr($obj, MAST::Call, '@!flags', @flags);
        nqp::bindattr($obj, MAST::Call, '@!args', @args);
        nqp::bindattr($obj, MAST::Call, '$!result', $result);
        nqp::bindattr_i($obj, MAST::Call, '$!op', $op);
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

    method dump_lines(@lines, $indent) {
        nqp::push(@lines, $indent~"MAST::Call");
        nqp::push(@lines, "$indent  target:");
        nqp::push(@lines, $!target.dump($indent ~ '    '));
        nqp::push(@lines, "$indent  result:");
        nqp::push(@lines, $!result.dump($indent ~ '    '));
        nqp::push(@lines, "$indent  flags:");
        for @!flags -> $flag {
            my $str := "$indent   ";
            if $flag +& $Arg::named {
                $str := $str ~ " named";
            }
            elsif $flag +& $Arg::flat {
                $str := $str ~ " flat";
            }
            elsif $flag +& $Arg::flatnamed {
                $str := $str ~ " flat/named";
            }
            else {
                $str := $str ~ " positional" ;
            }
            $str := $str ~ " obj" if $flag +& $Arg::obj;
            $str := $str ~ " int" if $flag +& $Arg::int;
            $str := $str ~ " num" if $flag +& $Arg::num;
            $str := $str ~ " str" if $flag +& $Arg::str;
            nqp::push(@lines, $str);
        }
        nqp::push(@lines, "$indent  args:");
        nqp::push(@lines, $_.dump($indent ~ '    ')) for @!args;
    }
}

# A series of instructions that fall on a particular line in a particular source file
class MAST::Annotated is MAST::Node {
    has str $!file;
    has int $!line;
    has @!instructions;

    method new(:$file = '<anon>', :$line!, :@instructions!) {
        my $obj := nqp::create(self);
        nqp::bindattr_s($obj, MAST::Annotated, '$!file', $file);
        nqp::bindattr_i($obj, MAST::Annotated, '$!line', $line);
        nqp::bindattr($obj, MAST::Annotated, '@!instructions', @instructions);
        $obj
    }

    method dump_lines(@lines, $indent) {
        nqp::push(@lines, $indent~"MAST::Annotated: file: $!file, line: $!line, instructions:");
        nqp::push(@lines, $_.dump($indent ~ '  ')) for @!instructions;
    }
}

# Handler constants.
module HandlerAction {
    our $unwind_and_goto              := 0;
    our $unwind_and_goto_with_payload := 1;
    our $invoke_and_we'll_see         := 2;
}

# Category constants.
module HandlerCategory {
    our $catch   := 1;
    our $control := 2;
    our $next    := 4;
    our $redo    := 8;
    our $last    := 16;
    our $return  := 32;
    our $unwind  := 64;
    our $take    := 128;
    our $warn    := 256;
    our $succeed := 512;
    our $proceed := 1024;
    our $labeled := 4096;
    our $await   := 8192;
    our $emit    := 16384;
    our $done    := 32768;
}

# A region with a handler.
class MAST::HandlerScope is MAST::Node {
    has @!instructions;
    has int $!category_mask;
    has int $!action;
    has $!goto_label;
    has $!block_local;
    has $!label_local;

    method new(:@instructions!, :$category_mask!, :$action!, :$goto!, :$block, :$label) {
        my $obj := nqp::create(self);
        nqp::bindattr($obj, MAST::HandlerScope, '@!instructions', @instructions);
        nqp::bindattr_i($obj, MAST::HandlerScope, '$!category_mask', $category_mask);
        nqp::bindattr_i($obj, MAST::HandlerScope, '$!action', $action);
        if nqp::istype($goto, MAST::Label) {
            nqp::bindattr($obj, MAST::HandlerScope, '$!goto_label', $goto);
        }
        else {
            nqp::die("Handler needs a MAST::Label to unwind to");
        }
        if $action == $HandlerAction::invoke_and_we'll_see {
            if nqp::istype($block, MAST::Local) {
                nqp::bindattr($obj, MAST::HandlerScope, '$!block_local', $block);
            }
            else {
                nqp::die("Handler action invoke-and-we'll-see needs a MAST::Local to invoke");
            }
        }
        elsif $action != $HandlerAction::unwind_and_goto &&
              $action != $HandlerAction::unwind_and_goto_with_payload {
            nqp::die("Unknown handler action");
        }
        if $category_mask +& $HandlerCategory::labeled {
            if nqp::istype($label, MAST::Local) {
                nqp::bindattr($obj, MAST::HandlerScope, '$!label_local', $label);
            }
            else {
                nqp::die("Handler category 'labeled' needs a MAST::Local");
            }
        }
        $obj
    }
}
