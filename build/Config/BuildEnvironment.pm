package Config::BuildEnvironment;
use strict;
use warnings;

sub detect {
    my %config;
    
    if ($^O =~ /MSWin32/) {
        # Windows.
        $config{'os'} = 'Windows';
        
        # We support the Microsoft toolchain.
        if (can_run('cl /nologo /?')) {
            # Ensure we have the other bits.
            return (excuse => 'It appears you have the MS C compiler, but no link!')
                unless can_run('link /nologo /?');
            return (excuse => 'It appears you have the MS C compiler, but no nmake!')
                unless can_run('nmake /nologo /?');
            
            # Set configuration flags.
            $config{'cc'}      = 'cl';
            $config{'cflags'}  = '/nologo /Zi -DWIN32';
            $config{'link'}    = 'link';
            $config{'ldflags'} = '/nologo /debug /NODEFAULTLIB kernel32.lib ws2_32.lib msvcrt.lib oldnames.lib advapi32.lib shell32.lib';
            $config{'make'}    = 'nmake';
            $config{'exe'}     = '.exe';
            $config{'o'}       = '.obj';
            $config{'rm'}      = 'del';
        }
        else {
            return (excuse => 'So far, we only support building with the Microsoft toolchain on Windows.');
        }
        
        return %config;
    }
    elsif ($^O =~ /linux/) {
        $config{'os'} = 'Linux';
        
        if (can_run('gcc')) {
            $config{'cc'}      = 'gcc';
            $config{'cflags'}  = '-c';
            $config{'link'}    = 'gcc';
            $config{'ldflags'} = '';
            $config{'make'}    = 'make';
            $config{'exe'}     = '';
            $config{'o'}       = '.o';
            $config{'rm'}      = 'rm';
        }
        else {
            return (excuse => 'So far, we only support building with gcc on Linux.');
        }
        
        return %config;
    }
    
    return (excuse => 'No recognized operating system or compiler found.'."  found: $^O");
}

sub can_run {
    my $try = shift;
    return `$try 2>&1` ne '';
}

'Leffe';
