use MASTCompiler;
use QASTCompilerMAST;
use NQPP6QRegex;
pir::load_bytecode__vs('dumper.pbc');

my $moarvm;
my $del;
my $copy;
my $outputnull;
my $quote;
#?if parrot
my %conf := pir::getinterp__P()[pir::const::IGLOBALS_CONFIG_HASH];
my $os := %conf<platform>;
#?endif
#?if jvm
#my %conf := nqp::jvmgetproperties(); XXX uncomment when fudging works
#my $os := %conf<os.name>;
#?endif
if nqp::lc($os) ~~ /^(win|mswin)/ {
    $moarvm := '..\\moarvm';
    $del := 'del /Q';
    $copy := 'copy /Y';
    $outputnull := 'NUL';
    $quote := '"';
}
else {
    # unix
    $moarvm := '../moarvm';
    $del := 'rm -f';
    $copy := 'cp';
    $outputnull := '/dev/null';
    $quote := "'";
}

my $env := nqp::getenvhash();
my $DEBUG := $env<MVMDEBUG>;
my $*COERCE_ARGS_OBJ := 0;

sub mast_frame_output_is($frame_filler, $expected, $desc, :$timeit, :$approx) is export {
    # Create frame
    my $frame := MAST::Frame.new();

    # Wrap in a compilation unit.
    my $comp_unit := MAST::CompUnit.new();
    $comp_unit.add_frame($frame);

    # fill with instructions
    $frame_filler($frame, $frame.instructions, $comp_unit);

    mast_output_is($comp_unit, $expected, $desc, $timeit, timeit => $timeit, approx => $approx);
}

sub mast_output_is($comp_unit, $expected, $desc, :$timeit, :$approx) is export {

    my $desc_file := $DEBUG ?? nqp::join('', match($desc, /(\w | ' ')+/, :global)) !! '';

    # Compile it.
    if $DEBUG {
        my $fh := pir::new__Ps('FileHandle');
        $fh.open("$desc_file.mastdump", "w");
        $fh.encoding('utf8');
        $fh.print("MAST: \n" ~ $comp_unit.DUMP());
        $fh.close();
    }
    MAST::Compiler.compile($comp_unit, 'temp.moarvm');
    pir::spawnw__Is("$copy temp.moarvm $quote$desc_file.moarvm$quote >$outputnull") if $DEBUG;

    # Invoke and redirect output to a file.
    my $start := nqp::time_n();
    pir::spawnw__Is("$moarvm --dump temp.moarvm > $quote$desc_file.mvmdump$quote") if $DEBUG;
    pir::spawnw__Is("$moarvm temp.moarvm foobar foobaz > temp.output");
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

sub qast_output_is($qast, $expected, $desc, :$timeit, :$approx) is export {
    mast_output_is(QAST::MASTCompiler.to_mast($qast), $expected, $desc, timeit => $timeit, approx => $approx);
}

sub qast_test($qast_builder, *@pos, *%named) is export {
    qast_output_is($qast_builder(), |@pos, |%named)
}

sub op(@ins, $op, *@args) is export {
    my $bank;
    for MAST::Ops.WHO {
        $bank := ~$_ if nqp::existskey(MAST::Ops.WHO{~$_}, $op);
    }
    nqp::die("unable to resolve MAST op '$op'") unless $bank;
    nqp::push(@ins, MAST::Op.new(
            :bank(nqp::substr($bank, 1)), :op($op), |@args
        ));
}

sub label($name) is export {
    MAST::Label.new( :name($name) )
}

sub ival($val) is export {
    MAST::IVal.new( :value($val) )
}

sub nval($val) is export {
    MAST::NVal.new( :value($val) )
}

sub sval($val) is export {
    MAST::SVal.new( :value($val) )
}

sub const($frame, $val) is export {
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

sub local($frame, $type) is export {
    return MAST::Local.new(:index($frame.add_local($type)));
}

sub lexical($frame, $type, $name) is export {
    my $idx := $frame.add_lexical($type, $name);
    return MAST::Lexical.new(:index($idx));
}

sub call(@ins, $target, @flags, :$result, *@args) is export {
    nqp::push(@ins, MAST::Call.new(
            :target($target), :result($result), :flags(@flags), |@args
        ));
}

sub annotated(@ins, $file, $line) is export {
    MAST::Annotated.new( :file($file), :line($line), :instructions(@ins) )
}

sub rxqast($str) is export {
    my $grammar := QRegex::P6Regex::Grammar.new();
    my $actions := QRegex::P6Regex::Actions.new();
    my $ast := $grammar.parse($str, p => 0, actions => $actions, rule => 'nibbler').ast;
    my $caught := 0;
    try {
        $ast.HOW;
        CATCH {
            $caught := 0;
            say("type: " ~ pir::typeof__SP($ast));
        }
    }
    #say((Q:PIR { %r = get_root_global ['parrot'], '_dumper' })($ast));
    #say("type: " ~ $ast.HOW.name($ast)) unless $caught;
    $ast
}
