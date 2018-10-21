use MASTOps;

my int $MVM_reg_void            := 0; # not really a register; just a result/return kind marker
my int $MVM_reg_int8            := 1;
my int $MVM_reg_int16           := 2;
my int $MVM_reg_int32           := 3;
my int $MVM_reg_int64           := 4;
my int $MVM_reg_num32           := 5;
my int $MVM_reg_num64           := 6;
my int $MVM_reg_str             := 7;
my int $MVM_reg_obj             := 8;
my int $MVM_reg_uint8           := 17;
my int $MVM_reg_uint16          := 18;
my int $MVM_reg_uint32          := 19;
my int $MVM_reg_uint64          := 20;

my int $MVM_operand_literal     := 0;
my int $MVM_operand_read_reg    := 1;
my int $MVM_operand_write_reg   := 2;
my int $MVM_operand_read_lex    := 3;
my int $MVM_operand_write_lex   := 4;
my int $MVM_operand_rw_mask     := 7;

my int $MVM_operand_int8        := ($MVM_reg_int8 * 8);
my int $MVM_operand_int16       := ($MVM_reg_int16 * 8);
my int $MVM_operand_int32       := ($MVM_reg_int32 * 8);
my int $MVM_operand_int64       := ($MVM_reg_int64 * 8);
my int $MVM_operand_num32       := ($MVM_reg_num32 * 8);
my int $MVM_operand_num64       := ($MVM_reg_num64 * 8);
my int $MVM_operand_str         := ($MVM_reg_str * 8);
my int $MVM_operand_obj         := ($MVM_reg_obj * 8);
my int $MVM_operand_ins         := (9 * 8);
my int $MVM_operand_type_var    := (10 * 8);
my int $MVM_operand_lex_outer   := (11 * 8);
my int $MVM_operand_coderef     := (12 * 8);
my int $MVM_operand_callsite    := (13 * 8);
my int $MVM_operand_type_mask   := (31 * 8);
my int $MVM_operand_uint8       := ($MVM_reg_uint8 * 8);
my int $MVM_operand_uint16      := ($MVM_reg_uint16 * 8);
my int $MVM_operand_uint32      := ($MVM_reg_uint32 * 8);
my int $MVM_operand_uint64      := ($MVM_reg_uint64 * 8);

class MAST::Bytecode is repr('VMArray') is array_type(uint8) {
    method new() {
        nqp::create(self)
    }
    method write_s(str $s) {
        my @subbuf := nqp::encode($s, 'utf8', nqp::create(self));
        self.write_buf(@subbuf);
    }
    method write_double(num $n) {
        nqp::writenum(self, nqp::elems(self), $n, 0);
    }
    method write_uint32(uint32 $i) {
        nqp::writeuint(self, nqp::elems(self), $i, 4);
    }
    method write_uint64(uint64 $i) {
        nqp::writeuint(self, nqp::elems(self), $i, 6);
    }
    method read_uint32_at(uint32 $pos) {
          nqp::bitshiftl_i(nqp::atpos_i(self, $pos + 3), 24)
        + nqp::bitshiftl_i(nqp::atpos_i(self, $pos + 2), 16)
        + nqp::bitshiftl_i(nqp::atpos_i(self, $pos + 1), 8)
        + nqp::atpos_i(self, $pos)
    }
    method write_uint32_at(uint32 $i, uint32 $pos) {
        nqp::writeuint(self, $pos, $i, 4);
    }
    method write_uint16(uint16 $i) {
        nqp::writeuint(self, nqp::elems(self), $i, 2);
    }
    method write_uint8(uint8 $i) {
        nqp::writeuint(self, nqp::elems(self), $i, 0);
    }
    method write_buf(@buf) {
        nqp::splice(self, @buf, nqp::elems(self), 0);
    }
    method dump() {
        note(nqp::elems(self) ~ " bytes");
        for self {
            note($_);
        }
    }
}

my %uint_map;
my %int_map;
my %num_map;
%int_map<8> := 1;
%int_map<16> := 2;
%int_map<32> := 3;
%int_map<64> := 4;
%num_map<32> := 5;
%num_map<64> := 6;
%uint_map<8> := 17;
%uint_map<16> := 18;
%uint_map<32> := 19;
%uint_map<64> := 20;
sub type_to_local_type($t) {
    my $spec := nqp::objprimspec($t);
    if $spec == 0 {
        8
    }
    elsif $spec == 1 {
        (nqp::objprimunsigned($t) ?? %uint_map !! %int_map){nqp::objprimbits($t)}
    }
    elsif $spec == 2 {
        %num_map{nqp::objprimbits($t)}
    }
    elsif $spec == 3 {
        7
    }
    else {
        nqp::die("Unknwon local type: " ~ $t.HOW.name($t) ~ ": " ~ $spec);
    }
}

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

    # Serialized data.
    has $!serialized;
    has $!string_heap;

    has $!writer;

    method BUILD(:$writer) {
        $!writer := $writer;
        @!frames := nqp::list;
        @!sc_handles := nqp::list;
        %!sc_lookup  := nqp::hash;
        @!extop_sigs := nqp::list;
        %!extop_idx  := nqp::hash;
        @!extop_names := nqp::list;
    }

    method writer() {
        $!writer
    }

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

    method serialized($serialized?) {
        nqp::defined($serialized)
            ?? ($!serialized := $serialized)
            !! $!serialized
    }

    method string_heap($string_heap?) {
        nqp::defined($string_heap)
            ?? ($!string_heap := $string_heap)
            !! $!string_heap
    }

    method add_strings(@strings) {
        for @strings {
            $!writer.add-string($_ || '');
        }
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

# Literal values.
class MAST::SVal is MAST::Node {
    method new(:$value!) {
        $value
    }
}
class MAST::IVal is MAST::Node {
    method new(:$value!, :$size = 64, :$signed = 1) {
        $value
    }
}
class MAST::NVal is MAST::Node {
    method new(:$value!, :$size = 64) {
        $value
    }
}

# A local lookup.
class MAST::Local is MAST::Node {
    has uint $!index is box_target;

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
    method frames_out() { $!frames_out }

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

# An operation to be executed. The operands must be either registers,
# literals or labels (depending on what the instruction needs).
class MAST::Op is MAST::Node {
    my %op_codes := MAST::Ops.WHO<%codes>;
    my @op_names := MAST::Ops.WHO<@names>;
    my %generators := MAST::Ops.WHO<%generators>;

    method new(str :$op!, *@operands) {
        self.new_with_operand_array(@operands, :$op)
    }

    method new_with_operand_array(@operands, str :$op!) {
        %generators{$op}(|@operands)
    }
}

# An extension operation to be executed. The operands must be either
# registers, literals or labels (depending on what the instruction needs).
class MAST::ExtOp is MAST::Node {
    method new(str :$op!, :$cu!, *@operands) {
        self.new_with_operand_array(@operands, :$op, :$cu)
    }

    method new_with_operand_array(@operands, str :$op!, :$cu!) {
        my $bytecode := $*MAST_FRAME.bytecode;
        my int $op_code := $cu.get_extop_code($op);

        my @extop_sigs := nqp::getattr($*MAST_FRAME.compunit, MAST::CompUnit, '@!extop_sigs');
        nqp::die("Invalid extension op $op specified")
            if $op_code < 1024 || $op_code - 1024 >= nqp::elems(@extop_sigs); # EXTOP_BASE
        my @operand_sigs := @extop_sigs[$op_code - 1024];

        $bytecode.write_uint16($op_code);

        my $num_operands := nqp::elems(@operand_sigs);
        nqp::die("Instruction has invalid number of operads")
            if nqp::elems(@operands) != $num_operands;
        my int $idx := 0;
        while $idx < $num_operands {
            my $flags := nqp::atpos_i(@operand_sigs, $idx);
            my $rw    := $flags +& $MVM_operand_rw_mask;
            my $type  := $flags +& $MVM_operand_type_mask;
            $*MAST_FRAME.compile_operand($bytecode, $rw, $type, @operands[$idx]);
            $idx++;
        }
    }
}

# A call. A register holding the thing to call should be specified, along
# with a set of flags describing the call site, followed by the arguments
# themselves, which may be constants or come from registers. There is also
# a set of flags, describing each argument. Some flags need two actual
# arguments, one specifying the name, the next the actual value.
class MAST::Call is MAST::Node {
    method new(:$target!, :@flags!, :$result = MAST::Node, :$op = 0, *@argvalues) {
        sanity_check(@flags, @argvalues);
        my $frame := $*MAST_FRAME;
        my $bytecode := $frame.bytecode;
        my $callsite-id := $frame.callsites.get_callsite_id(@flags, @argvalues);

        $bytecode.write_uint16(%MAST::Ops::codes<prepargs>);
        $bytecode.write_uint16($callsite-id);

        my $call_op :=
            $op == 1
                ?? %MAST::Ops::codes<nativeinvoke_v>
                !! $op == 2
                    ?? %MAST::Ops::codes<speshresolve>
                    !! %MAST::Ops::codes<invoke_v>;

        my uint16 $arg_pos := $op == 1 ?? 1 !! 0;
        my uint16 $arg_out_pos := 0;
        for @flags -> $flag {
            if $flag +& $Arg::named {
                $bytecode.write_uint16(%MAST::Ops::codes<argconst_s>);
                $bytecode.write_uint16($arg_out_pos);
                $frame.compile_operand($bytecode, 0, $MVM_operand_str, @argvalues[$arg_pos]);
                $arg_pos++;
                $arg_out_pos++;
            }
            elsif $flag +& $Arg::flat {
                nqp::die("Illegal flat arg to speshresolve") if $op == 2;
            }

            if $op == 2 && !($flag +& $Arg::obj) {
                nqp::die("Illegal non-object arg to speshresolve");
            }
            if $flag +& $Arg::obj {
                $bytecode.write_uint16(%MAST::Ops::codes<arg_o>);
                $bytecode.write_uint16($arg_out_pos);
                $frame.compile_operand($bytecode, $MVM_operand_read_reg, $MVM_operand_obj, @argvalues[$arg_pos]);
            }
            elsif $flag +& $Arg::str {
                $bytecode.write_uint16(%MAST::Ops::codes<arg_s>);
                $bytecode.write_uint16($arg_out_pos);
                $frame.compile_operand($bytecode, $MVM_operand_read_reg, $MVM_operand_str, @argvalues[$arg_pos]);
            }
            elsif $flag +& $Arg::int {
                $bytecode.write_uint16(%MAST::Ops::codes<arg_i>);
                $bytecode.write_uint16($arg_out_pos);
                $frame.compile_operand($bytecode, $MVM_operand_read_reg, $MVM_operand_int64, @argvalues[$arg_pos]);
            }
            elsif $flag +& $Arg::num {
                $bytecode.write_uint16(%MAST::Ops::codes<arg_n>);
                $bytecode.write_uint16($arg_out_pos);
                $frame.compile_operand($bytecode, $MVM_operand_read_reg, $MVM_operand_num64, @argvalues[$arg_pos]);
            }
            else {
                nqp::die("Unhandled arg type $flag");
            }
            $arg_pos++;
            $arg_out_pos++;
        }

	my $res_type;
        if $op == 2 {
            nqp::die('speshresolve must have a result')
                unless $result.isa(MAST::Local);
            nqp::die('MAST::Local index out of range')
                if $result >= nqp::elems($frame.local_types);
            nqp::die('speshresolve must have an object result')
                if type_to_local_type($frame.local_types()[$result]) != $MVM_reg_obj;
            $res_type := $MVM_operand_obj;
        }
        elsif $result.isa(MAST::Local) {
            my @local_types := $frame.local_types;
            my $index := $result;
            if $index >= nqp::elems(@local_types) {
                nqp::die("MAST::Local index out of range");
            }
            my $op_name := $op == 0 ?? 'invoke_' !! 'nativeinvoke_';
            if nqp::objprimspec(@local_types[$index]) == 1 {
                $op_name := $op_name ~ 'i';
                $res_type := $MVM_operand_int64;
            }
            elsif nqp::objprimspec(@local_types[$index]) == 2 {
                $op_name := $op_name ~ 'n';
                $res_type := $MVM_operand_num64;
            }
            elsif nqp::objprimspec(@local_types[$index]) == 3 {
                $op_name := $op_name ~ 's';
                $res_type := $MVM_operand_str;
            }
            elsif nqp::objprimspec(@local_types[$index]) == 0 { # object
                $op_name := $op_name ~ 'o';
                $res_type := $MVM_operand_obj;
            }
            else {
                nqp::die('Invalid MAST::Local type ' ~ @local_types[$index] ~ ' for return value ' ~ $index);
            }
            $call_op := %MAST::Ops::codes{$op_name};
        }

        $bytecode.write_uint16($call_op);
        if $call_op != %MAST::Ops::codes<invoke_v> && $call_op != %MAST::Ops::codes<nativeinvoke_v> {
            $frame.compile_operand($bytecode, $MVM_operand_read_reg, $res_type, $result);
        }
        if $op == 2 {
            $frame.compile_operand($bytecode, $MVM_operand_literal, $MVM_operand_str, $target);
        }
        else {
            $frame.compile_operand($bytecode, $MVM_operand_read_reg, $MVM_operand_obj, $target);
        }
        if $op == 1 {
            $frame.compile_operand($bytecode, $MVM_operand_read_reg, $MVM_operand_obj, @argvalues[0]);
        }
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

# A series of instructions that fall on a particular line in a particular source file
class MAST::Annotated is MAST::Node {
    method new(:$file = '<anon>', :$line!) {
        $*MAST_FRAME.add-annotation(:$file, :$line);
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
    method new(:$start, :$category_mask!, :$action!, :$goto!, :$block, :$label) {
        unless nqp::istype($goto, MAST::Label) {
            nqp::die("Handler needs a MAST::Label to unwind to");
        }
        if $action == $HandlerAction::invoke_and_we'll_see {
            unless nqp::istype($block, MAST::Local) {
                nqp::die("Handler action invoke-and-we'll-see needs a MAST::Local to invoke");
            }
        }
        elsif $action != $HandlerAction::unwind_and_goto &&
              $action != $HandlerAction::unwind_and_goto_with_payload {
            nqp::die("Unknown handler action");
        }
        if $category_mask +& $HandlerCategory::labeled {
            unless nqp::istype($label, MAST::Local) {
                nqp::die("Handler category 'labeled' needs a MAST::Local");
            }
        }
        $*MAST_FRAME.add-handler-scope(:$start, :$category_mask, :$action, :$goto, :$block, :$label);
    }
}

sub get_typename($type) {
    ["obj","int","num","str"][nqp::objprimspec($type)]
}

class MoarVM::Handler {
    has int32 $!start_offset;
    has int32 $!end_offset;
    has int32 $!category_mask;
    has $!action;
    has $!label;
    has $!label_reg;
    has $!local;
    method BUILD(:$start_offset, :$end_offset, :$category_mask, :$action, :$label) {
        $!start_offset  := $start_offset;
        $!end_offset    := $end_offset;
        $!category_mask := $category_mask;
        $!action        := $action;
        $!label         := $label;
    }
    method start_offset()    { $!start_offset }
    method end_offset()      { $!end_offset }
    method category_mask()   { $!category_mask }
    method action()          { $!action }
    method label()           { $!label }
    method label_reg()       { $!label_reg }
    method set_label_reg($l) { $!label_reg := $l }
    method local()           { $!local }
    method set_local($l)     { $!local := $l }
    method add-offset(int32 $offset) {
        $!start_offset := $!start_offset + $offset;
        $!end_offset := $!end_offset + $offset;
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

    # The outer frame, if any.
    has $!outer;

    # Mapping of lexical names to lexical index, for lookups.
    has %!lexical_map;

    # Flag bits.
    my int $FRAME_FLAG_EXIT_HANDLER := 1;
    my int $FRAME_FLAG_IS_THUNK     := 2;
    my int $FRAME_FLAG_HAS_CODE_OBJ := 4;
    my int $FRAME_FLAG_NO_INLINE    := 8;
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

    has $!writer;
    has $!compunit;
    has $!string-heap;
    has $!callsites;

    has uint32 $!cuuid-idx;
    has uint32 $!name-idx;
    has $!bytecode;
    has uint32 $!bytecode-offset;
    has %!labels;
    has %!label-fixups;
    has @!child-label-fixups;
    has @!lexical_names_idxs;
    has $!annotations;
    has $!annotations-offset;
    has $!num-annotations;
    has @!handlers;
    has $!saved-bytecode;
    has $!saved-annotations-offset;

    method start_prologue() {
        if !nqp::isnull($!saved-bytecode) {
            nqp::die("Nested start_prologue detected!");
        }
        $!saved-bytecode := $!bytecode;
        $!bytecode := nqp::create(MAST::Bytecode);
        $!saved-annotations-offset := nqp::elems($!annotations);
    }

    method end_prologue() {
        my int32 $offset := nqp::elems($!bytecode);
        for @!child-label-fixups {
            my int32 $at := $_;
            my int32 $pos := $!saved-bytecode.read_uint32_at($at);
            $pos := $pos + $offset;
            $!saved-bytecode.write_uint32_at($pos, $at);
        }
        @!child-label-fixups := nqp::list;
        $!bytecode.write_buf($!saved-bytecode);
        $!saved-bytecode := nqp::null();
        for %!labels {
            my int32 $pos := nqp::iterval($_);
            $pos := $pos + $offset;
            %!labels{nqp::iterkey_s($_)} := $pos;
        }
        for @!handlers {
            $_.add-offset($offset);
        }
        my int32 $at := 0;
        my int32 $end := $!saved-annotations-offset;
        my int32 $ann-size := 3 * 4;
        while $at < $end {
            my int32 $pos := $!annotations.read_uint32_at($at);
            $pos := $pos + $offset;
            $!annotations.write_uint32_at($pos, $at);
            $at := $at + $ann-size;
        }
    }

    my int $cuuid_src := 0;
    sub fresh_id() {
        $cuuid_src++;
        "!MVM_CUUID_$cuuid_src"
    }

    method new(:$cuuid = fresh_id(), :$name = '<anon>', :$writer, :$compunit) {
        my $obj := nqp::create(self);
        $obj.BUILD($cuuid, $name, $writer, $compunit);
        $obj
    }

    method BUILD($cuuid, $name, $writer, $compunit) {
        $!cuuid              := $cuuid;
        $!name               := $name;
        @!lexical_types      := nqp::list();
        @!lexical_names      := nqp::list();
        @!local_types        := nqp::list();
        $!outer              := MAST::Node;
        %!lexical_map        := nqp::hash();
        @!static_lex_values  := nqp::list_i();
        $!writer             := $writer;
        $!compunit           := $compunit;
        $!string-heap        := $writer.string-heap;
        $!callsites          := $writer.callsites;
        $!annotations        := MAST::Bytecode.new;
        $!annotations-offset := nqp::elems($!annotations);
        $!num-annotations    := 0;
        $!bytecode           := MAST::Bytecode.new;
        $!cuuid-idx          := $!string-heap.add($!cuuid);
        $!name-idx           := $!string-heap.add($!name);
        @!handlers           := nqp::list;
        %!labels             := nqp::hash;
        %!label-fixups       := nqp::hash;
        @!lexical_names_idxs := nqp::list;
        $!saved-bytecode     := nqp::null;
        @!child-label-fixups := nqp::list;
    }

    method prepare() {
        for @!lexical_names {
            nqp::push(@!lexical_names_idxs, self.add-string($_));
        }
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

    method set_outer($outer) {
        if nqp::istype($outer, MAST::Frame) {
            $!outer := $outer;
        }
        else {
            nqp::die("set_outer expects a MAST::Frame");
        }
    }

    method cuuid()  { $!cuuid }
    method name()   { $!name }
    method outer()  { $!outer }
    method writer() { $!writer }

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

    method no_inline($value = -1) {
        if $value > 0 {
            $!flags := nqp::bitor_i($!flags, $FRAME_FLAG_NO_INLINE);
        }
        nqp::bitand_i($!flags, $FRAME_FLAG_NO_INLINE)
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
            nqp::push(@lines, '');
        }
    }

    method cuuid-idx() { $!cuuid-idx }
    method name-idx() { $!name-idx }
    method bytecode() { $!bytecode }
    method bytecode-length() { nqp::elems($!bytecode) }
    method bytecode-offset() { $!bytecode-offset }
    method annotations() { $!annotations }
    method annotations-offset() { $!annotations-offset }
    method set-annotations-offset($offset) { $!annotations-offset := $offset }
    method set-bytecode-offset($offset) { $!bytecode-offset := $offset }
    method local_types() {
        @!local_types
    }
    method lexical_types() {
        @!lexical_types
    }
    method lexical-name-idxs() { @!lexical_names_idxs }
    method static_lex_values() {
        @!static_lex_values
    }
    method flags() {
        $!flags
    }
    method code_obj_sc_dep_idx() {
        $!code_obj_sc_dep_idx
    }
    method code_obj_sc_idx() {
        $!code_obj_sc_idx
    }
    method num-annotations() { $!num-annotations }
    method handlers() { @!handlers }
    method size() {
        my uint32 $size := 50
            + 2  * nqp::elems(self.local_types)
            + 6  * nqp::elems(self.lexical_types)
            + 12 * nqp::elems(self.static_lex_values) / 4;
        for @!handlers {
            $size := $size + 20;
            if $_.category_mask +& 4096 {
                $size := $size + 2;
            }
        }
        $size
    }

    method add-string(str $s) {
        $!string-heap.add($s);
    }
    method callsites() { $!callsites }
    method compunit()  { $!compunit }
    method labels() { %!labels }
    method label-fixups() { %!label-fixups }
    method resolve-label($label) {
        %!labels{nqp::objectid($label)}
    }

    method add-label(MAST::Label $i) {
        my $pos := nqp::elems($!bytecode);
        my $key := nqp::objectid($i);
        if %!labels{$key} {
            nqp::die("Duplicate label at $pos");
        }
        %!labels{$key} := $pos;
        if nqp::existskey(%!label-fixups, $key) {
            for %!label-fixups{$key} {
                my int32 $at := $_[1];
                $_[0].write_uint32_at($pos, $at);
            }
        }
    }

    my @kind_to_args := [0,
        $Arg::int,  # $MVM_reg_int8            := 1;
        $Arg::int,  # $MVM_reg_int16           := 2;
        $Arg::int,  # $MVM_reg_int32           := 3;
        $Arg::int,  # $MVM_reg_int64           := 4;
        $Arg::num,  # $MVM_reg_num32           := 5;
        $Arg::num,  # $MVM_reg_num64           := 6;
        $Arg::str,  # $MVM_reg_str             := 7;
        $Arg::obj   # $MVM_reg_obj             := 8;
    ];
    method compile_label($bytecode, $arg) {
        my $key := nqp::objectid($arg);
        my %labels := self.labels;
        if nqp::isnull($!saved-bytecode) {
            nqp::push(@!child-label-fixups, nqp::elems($!bytecode));
        }
        if nqp::existskey(%labels, $key) {
            $bytecode.write_uint32(%labels{$key});
        }
        else {
            my %label-fixups := self.label-fixups;
            my @fixups := nqp::existskey(%label-fixups, $key)
                ?? %label-fixups{$key}
                !! (%label-fixups{$key} := nqp::list);
            nqp::push(@fixups, [$bytecode, nqp::elems($bytecode)]);
            $bytecode.write_uint32(0);
        }
    }
    method compile_operand($bytecode, int $rw, int $type, $arg) {
        if $rw == nqp::const::MVM_OPERAND_LITERAL {
            if $type == nqp::const::MVM_OPERAND_INT64 {
                my int $value := $arg;
                $bytecode.write_uint64($value);
            }
            elsif $type == nqp::const::MVM_OPERAND_INT16 {
                my int $value := $arg;
                if $value < -32768 || 32767 < $value {
                    nqp::die("Value outside range of 16-bit MAST::IVal");
                }
                $bytecode.write_uint16($value);
            }
            elsif $type == nqp::const::MVM_OPERAND_NUM64 {
                $bytecode.write_double($arg);
            }
            elsif $type == nqp::const::MVM_OPERAND_STR {
                $bytecode.write_uint32(self.add-string($arg));
            }
            elsif $type == nqp::const::MVM_OPERAND_INS {
                self.compile_label($bytecode, $arg);
            }
            elsif $type == nqp::const::MVM_OPERAND_CODEREF {
                nqp::die("Expected MAST::Frame, but didn't get one")
                    unless $arg.isa(MAST::Frame);
                my $index := self.writer.get_frame_index($arg);
                $bytecode.write_uint16($index);
            }
            else {
                nqp::die("literal operand type $type NYI");
            }
        }
        elsif $rw == nqp::const::MVM_OPERAND_READ_REG || $rw == nqp::const::MVM_OPERAND_WRITE_REG {
            nqp::die("Expected MAST::Local, but didn't get one")
                unless $arg.isa(MAST::Local);

            my @local_types := self.local_types;
            my int $index := $arg;
            if $index > nqp::elems(@local_types) {
                nqp::die("MAST::Local index out of range");
            }
            my $local_type := @local_types[$index];
            if ($type != nqp::bitshiftl_i(type_to_local_type($local_type), 3) && $type != nqp::const::MVM_OPERAND_TYPE_VAR) {
                nqp::die("MAST::Local of wrong type specified");
            }

            $bytecode.write_uint16($index);
        }
        elsif $rw == nqp::const::MVM_OPERAND_READ_LEX || $rw == nqp::const::MVM_OPERAND_WRITE_LEX {
            nqp::die("Expected MAST::Lexical, but didn't get one")
                unless $arg.isa(MAST::Lexical);
            $bytecode.write_uint16($arg.index);
            $bytecode.write_uint16(nqp::getattr($arg, MAST::Lexical, '$!frames_out'));
        }
        else {
            nqp::die("Unknown operand mode $rw cannot be compiled");
        }
    }
    method add-annotation(:$file, :$line) {
        $!annotations.write_uint32(nqp::elems($!bytecode));
        $!annotations.write_uint32(self.add-string($file));
        $!annotations.write_uint32($line);
        $!num-annotations++;
    }
    method add-handler-scope(:$category_mask, :$action, :$goto, :$block, :$label, :$start!) {
        unless nqp::defined($start) {
            nqp::die("MAST::HandlerScope needs a start");
        }
        my $handler := MoarVM::Handler.new(
            :start_offset($start),
            :end_offset(nqp::elems($!bytecode)),
            :$category_mask,
            :$action,
            :label($goto),
        );
        nqp::push(@!handlers, $handler);
        if $category_mask +& 4096 { # MVM_EX_CATEGORY_LABELED
            nqp::die('MAST::Local required for HandlerScope with loop label')
                unless $label.isa(MAST::Local);
            nqp::die('MAST::Local index out of range in HandlerScope')
                if $label >= nqp::elems(self.local_types);
            nqp::die('MAST::Local for HandlerScope must be an object')
                if type_to_local_type(self.local_types()[$label]) != $MVM_reg_obj;
            $handler.set_label_reg($label.index);
        }
        if $action == 2 { # HANDLER_INVOKE
            $handler.set_local(nqp::getattr($block, MAST::Local, '$!index'));
        }
        elsif $action == 0 || $action == 1 { # HANDLER_UNWIND_GOTO || HANDLER_UNWIND_GOTO_OBJ 
            $handler.set_local(0);
        }
        else {
            nqp::die('Invalid action code for handler scope');
        }
    }
    method write_operand($i, $idx, $o) {
        my $flags := nqp::atpos_i(@MAST::Ops::values, nqp::atpos_i(@MAST::Ops::offsets, $i.op) + $idx);
        my $rw    := $flags +& $MVM_operand_rw_mask;
        my $type  := $flags +& $MVM_operand_type_mask;
        self.compile_operand($!bytecode, $rw, $type, $o);
    }
}
