#!/usr/bin/env nqp-m
use MASTNodes;

# copied from nqp/src/vm/moar/NQP/Ops.nqp

my $MVM_reg_int64           := 4;
my $MVM_reg_num64           := 6;
my $MVM_reg_str             := "7"; # yeah beats me too
my $MVM_reg_obj             := 8;

my $mc := nqp::getcomp('MAST');
if (nqp::isnull($mc)) {
    nqp::say("FUUUU");
    nqp::exit(1);
}

my $comp_unit := MAST::CompUnit.new();
my $frame := MAST::Frame.new();

my $pos_a := MAST::IVal.new(value => 0);
my $loc_a := MAST::Local.new(index => $frame.add_local($MVM_reg_int64));
my $getarg_a := MAST::Op.new(op => 'param_rp_i', $loc_a, $pos_a);

my $pos_b := MAST::IVal.new(value => 1);
my $loc_b := MAST::Local.new(index => $frame.add_local($MVM_reg_int64));
my $getarg_b := MAST::Op.new(op => 'param_rp_i', $loc_b, $pos_b);

my $loc_c := MAST::Local.new(index => $frame.add_local($MVM_reg_int64));
my $add_a_b_c := MAST::Op.new(op => 'add_i', $loc_c, $loc_a, $loc_b);

# Unplanned JIT sentinel :-)
my $val_s := MAST::SVal.new(value => "OH HAI");
my $loc_s := MAST::Local.new(index => $frame.add_local($MVM_reg_str));
my $store_s := MAST::Op.new(op => 'const_s', $loc_s, $val_s);
my $say_s := MAST::Op.new(op => 'say', $loc_s);

my $return_c := MAST::Op.new(op => 'return_i', $loc_c);

nqp::push($frame.instructions, $getarg_a);
nqp::push($frame.instructions, $getarg_b);
nqp::push($frame.instructions, $add_a_b_c);
nqp::push($frame.instructions, $store_s);
nqp::push($frame.instructions, $say_s);
nqp::push($frame.instructions, $return_c);

$comp_unit.add_frame($frame);

my $cub := $mc.assemble_and_load($comp_unit);
my $iv := nqp::compunitmainline($cub);

sub foo() {
    1 + 1;
}

my $i := 0;

while $i < 100 {
    $i++;
    my $x := foo();
    nqp::say("Return value: " ~ $x);
}





