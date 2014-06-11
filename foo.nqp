#!/usr/bin/env nqp-m
use MASTNodes;

my $MVM_reg_int64 := 4;

my $mc := nqp::getcomp('MAST');
if (nqp::isnull($mc)) {
    nqp::say("FUUUU");
    nqp::exit(1);
}

my $comp_unit := MAST::CompUnit.new();
my $frame := MAST::Frame.new();

my $val_a := MAST::IVal.new(value => 6);
my $loc_a := MAST::Local.new(index => $frame.add_local($MVM_reg_int64));
my $store_a := MAST::Op.new(op => 'const_i64', $loc_a, $val_a);

my $val_b := MAST::IVal.new(value => 7);
my $loc_b := MAST::Local.new(index => $frame.add_local($MVM_reg_int64));
my $store_b := MAST::Op.new(op => 'const_i64', $loc_b, $val_b);

my $loc_c := MAST::Local.new(index => $frame.add_local($MVM_reg_int64));
my $add_a_b_c := MAST::Op.new(op => 'add_i', $loc_c, $loc_a, $loc_b);

my $return_c := MAST::Op.new(op => 'return_i', $loc_c);

nqp::push($frame.instructions, $store_a);
nqp::push($frame.instructions, $store_b);
nqp::push($frame.instructions, $add_a_b_c);
nqp::push($frame.instructions, $return_c);

$comp_unit.add_frame($frame);

my $cub := $mc.assemble_and_load($comp_unit);
my $iv := nqp::compunitmainline($cub);

my $i := 0;
while $i < 100 {
    $i++;
    nqp::say("Loop nr " ~ $i);
    my $x := $iv();
}



