package build::auto;
use strict;
use warnings;

my %APRW64 = (
    %::APR,
    path  => '3rdparty/apr/x64/LibR',
    rule  => 'cd 3rdparty/apr && $(MAKE) -f Makefile.win ARCH="x64 Release" buildall',
    clean => 'cd 3rdparty/apr && $(MAKE) -f Makefile.win ARCH="x64 Release" clean',
);

sub detect_native {
    my ($config, $defaults) = @_;

    # detect x64 on Windows so we can build the correct APR and dyncall versions
    if ($config->{cc} eq 'cl') {
        print ::dots('    auto-detecting x64 toolchain');
        my $msg = `cl 2>&1`;
        if (defined $msg) {
            if ($msg =~ /x64/) {
                print "YES\n";
                $defaults->{-thirdparty}->{apr} = { %APRW64 };

    #            $defaults->{-thirdparty}->{dc}->{rule} =
    #                'cd 3rdparty/dyncall && configure.bat /target-x64 && $(MAKE) -f Nmakefile';
            }
            else { print "NO\n" }
        }
        else {
            ::softfail("could not run 'cl'");
            print ::dots('    assuming x86'), "OK\n";
        }
    }

    # detect readline and use it instead of linenoise
    if ($defaults->{-toolchain} eq 'gnu' && !defined $config->{hasreadline}) {
        print ::dots('    auto-detecting GNU readline');

        my $cc = $config->{cc};
        qx{echo '#include <readline/readline.h>' | $cc -E - 2>&1};

        if ($? >> 8 == 0) {
            print "YES\n";
            $config->{ldlibs} = '-lreadline ' . $config->{ldlibs};

            print ::dots('    disable Linenoise build');
            $defaults->{-thirdparty}->{ln} = undef;
            $config->{hasreadline} = 1;
            print "OK\n";
        }
        else { print "NO\n" }
    }
}

sub detect_cross {}

42;
