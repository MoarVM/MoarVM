use MASTCompiler;
use QASTCompilerMAST;

my $moarvm;
my $del;
my $copy;
my $outputnull;
pir::spawnw__Is("del /? >temp.output 2>&1");
my $out := slurp('temp.output');
if (!($out ~~ /Extensions/)) {
    # unix
    $moarvm := '../../moarvm';
    $del := 'rm -f';
    $copy := 'cp';
    $outputnull := '/dev/null';
}
else {
    $moarvm := '..\\..\\moarvm';
    $del := 'del /Q';
    $copy := 'copy /Y';
    $outputnull := 'NUL';
}

our sub mast_frame_output_is($frame_filler, $expected, $desc, :$timeit, :$approx) {
    # Create frame
    my $frame := MAST::Frame.new();
    
    # Wrap in a compilation unit.
    my $comp_unit := MAST::CompUnit.new();
    $comp_unit.add_frame($frame);
    
    # fill with instructions
    $frame_filler($frame, $frame.instructions, $comp_unit);
    
    mast_output_is($comp_unit, $expected, $desc, $timeit, timeit => $timeit, approx => $approx);
}

our sub mast_output_is($comp_unit, $expected, $desc, :$timeit, :$approx) {
    
    # Compile it.
    MAST::Compiler.compile($comp_unit, 'temp.moarvm');
    #pir::spawnw__Is("$copy temp.moarvm \"$desc.moarvm\" >$outputnull");

    # Invoke and redirect output to a file.
    my $start := nqp::time_n();
    pir::spawnw__Is("$moarvm temp.moarvm > temp.output");
    my $end := nqp::time_n();
    
    # Read it and check it is OK.
    my $output := slurp('temp.output');
    $output := subst($output, /\r\n/, "\n", :global);
    my $okness := $output eq $expected || ($approx && 0 + $output != 0 && 0.0 + $output - +$expected < 0.0001);
    ok($okness, $desc);
    say("                                     # " ~ ($end - $start) ~ " s") if $timeit;
    unless $okness {
        say("GOT:\n$output");
        say("EXPECTED:\n$expected");
    }
    
    pir::spawnw__Is("$del temp.moarvm");
    pir::spawnw__Is("$del temp.output");
}

our sub qast_output_is($qast, $expected, $desc, :$timeit, :$approx) {
    mast_output_is(QAST::MASTCompiler.to_mast($qast), $expected, $desc, timeit => $timeit, approx => $approx);
}

our sub op(@ins, $op, *@args) {
    my $bank;
    for MAST::Ops.WHO {
        $bank := ~$_ if nqp::existskey(MAST::Ops.WHO{~$_}, $op);
    }
    nqp::die("unable to resolve MAST op '$op'") unless $bank;
    nqp::push(@ins, MAST::Op.new(
            :bank(nqp::substr($bank, 1)), :op($op), |@args
        ));
}

our sub label($name) {
    MAST::Label.new( :name($name) )
}

our sub ival($val) {
    MAST::IVal.new( :value($val) )
}

our sub nval($val) {
    MAST::NVal.new( :value($val) )
}

our sub sval($val) {
    MAST::SVal.new( :value($val) )
}

our sub const($frame, $val) {
    my $type;
    my $op;
    if ($val ~~ MAST::SVal) {
        $type := str;
        $op := 'const_s';
    }
    elsif ($val ~~ MAST::NVal) {
        $type := num;
        $op := 'const_n64';
    }
    elsif ($val ~~ MAST::IVal) {
        $type := int;
        $op := 'const_i64';
    }
    else {
        nqp::die('invalid constant provided');
    }
    my $local := local($frame, $type);
    op($frame.instructions, $op, $local, $val);
    return $local;
}

our sub local($frame, $type) {
    return MAST::Local.new(:index($frame.add_local($type)));
}

our sub call(@ins, $target, @flags, :$result, *@args) {
    nqp::push(@ins, MAST::Call.new(
            :target($target), :result($result), :flags(@flags), |@args
        ));
}
