use MASTCompiler;

my $moarvm := '..\\..\\moarvm';

our sub mast_frame_output_is($frame_filler, $expected, $desc) {
    # Create frame; get it set up.
    my $frame := MAST::Frame.new();
    $frame_filler($frame);
    
    # Wrap in a compilation unit.
    my $comp_unit := MAST::CompUnit.new();
    $comp_unit.add_frame($frame);

    # Compile it.
    MAST::Compiler.compile($comp_unit, 'temp.moarvm');

    # Invoke and redirect output to a file.
    pir::spawnw__Is("$moarvm temp.moarvm > temp.output");
    
    # Read it and check it is OK.
    my $output := slurp('temp.output');
    $output := subst($output, /\r\n/, "\n", :global);
    my $okness := $output eq $expected;
    ok($okness, $desc);
    unless $okness {
        say("GOT:\n$output");
        say("EXPECTED:\n$expected");
    }
    
    # Clean up.
    pir::spawnw__Is("del /Q temp.moarvm");
    pir::spawnw__Is("del /Q temp.output");
}
