package Config::APR;
use strict;
use warnings;

sub configure {
    my %config = @_;
    
    if ($^O =~ /MSWin32/) {
        if ($config{'make'} eq 'nmake') {
            # XXX TODO: Stuff for linking it in.
            return (%config,
                # XXX Won't fly on 32-bit...
                apr_build_line => 'cd 3rdparty/apr && nmake -f Makefile.win ARCH="x64 Release" buildall',
                apr_lib => '3rdparty/apr/x64/LibR/apr-1.lib'
            );
        }
        else {
            return (excuse => "Don't know how to build APR on Windows without Microsoft toolchain");
        }
    }
    else {
        # Presumably we can run ./configure.
        return (excuse => 'Unimplemented on non-Windows platforms so far');
    }
}

'Yeti';
