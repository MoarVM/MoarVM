package Config::APR;
use strict;
use warnings;

sub configure {
    my %config = @_;
    
    if ($^O =~ /MSWin32/) {
        if ($config{'make'} eq 'nmake') {
            # XXX TODO: Stuff for linking it in.
            return %config;
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
