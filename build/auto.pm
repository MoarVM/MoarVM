package build::auto;
use strict;
use warnings;

sub detect_native {
    my ($config, $defaults) = @_;

    # detect x64 on Windows so we can build the correct dyncall version
    if ($config->{cc} eq 'cl') {
        print ::dots('    auto-detecting x64 toolchain');
        my $msg = `cl 2>&1`;
        if (defined $msg) {
            if ($msg =~ /x64/) {
                print "YES\n";
                $defaults->{-thirdparty}->{dc}->{rule} =
                    'cd 3rdparty/dyncall && configure.bat /target-x64 && $(MAKE) -f Nmakefile';
            }
            else { print "NO\n" }
        }
        else {
            ::softfail("could not run 'cl'");
            print ::dots('    assuming x86'), "OK\n";
        }
    }
    elsif ($defaults->{os} eq 'mingw32' && $defaults->{-toolchain} eq 'gnu') {
        print ::dots('    auto-detecting x64 toolchain');
        my $cc = $config->{cc};
        my $msg = `$cc -dumpmachine 2>&1`;
        if (defined $msg) {
            if ($msg =~ /x86_64/) {
                print "YES\n";

                $defaults->{-thirdparty}->{dc}->{rule} =
                    'cd 3rdparty/dyncall && ./configure.bat /target-x64 /tool-gcc && $(MAKE) -f Makefile.embedded mingw32';
            }
            else { print "NO\n" }
        }
        else {
            ::softfail("could not run 'cl'");
            print ::dots('    assuming x86'), "OK\n";
        }
    }
}

sub detect_cross {}

42;
