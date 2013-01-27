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
            $config{'cc'}           = 'cl';
            $config{'cflags'}       = '/nologo /Zi -DWIN32';
            $config{'couto'}        = '-Fo';
            $config{'link'}         = 'link';
            $config{'louto'}        = '-out:';
            $config{'ldflags'}      = '/nologo /debug /NODEFAULTLIB kernel32.lib ws2_32.lib msvcrt.lib mswsock.lib rpcrt4.lib oldnames.lib advapi32.lib shell32.lib';
            $config{'llibs'}        = '';
            $config{'make'}         = 'nmake';
            $config{'exe'}          = '.exe';
            $config{'o'}            = '.obj';
            $config{'rm'}           = 'del';
            $config{'noreturn'}     = '__declspec(noreturn)';
            $config{'noreturngcc'}  = '';
            $config{'cat'}          = 'TYPE';
        }
        else {
            return (excuse => 'So far, we only support building with the Microsoft toolchain on Windows.');
        }
        
        return %config;
    }
    elsif ($^O =~ /linux/) {
        $config{'os'} = 'Linux';
        
        if (can_run('clang')) {
            $config{'cc'}           = 'clang';
            $config{'cflags'}       = '-g -fsanitize=address -fno-omit-frame-pointer -fno-optimize-sibling-calls';
            $config{'couto'}        = '-o ';
            $config{'link'}         = 'clang';
            $config{'louto'}        = '-o ';
            $config{'ldflags'}      = '-L 3rdparty/apr/.libs -g -fsanitize=address';
            $config{'llibs'}        = '-Wl,-Bstatic -lapr-1 -Wl,-Bdynamic -lpthread -lm';
            $config{'make'}         = 'make';
            $config{'exe'}          = '';
            $config{'o'}            = '.o';
            $config{'rm'}           = 'rm -f';
            $config{'noreturn'}     = '';
            $config{'noreturngcc'}  = '__attribute__((noreturn))';
            $config{'cat'}          = 'cat';
        }
        elsif (can_run('gcc')) {
            $config{'cc'}           = 'gcc';
            $config{'cflags'}       = ' -D_REENTRANT -D_LARGEFILE64_SOURCE -Wno-format-security -g';
            $config{'couto'}        = '-o ';
            $config{'link'}         = 'gcc';
            $config{'louto'}        = '-o ';
            $config{'ldflags'}      = '-L 3rdparty/apr/.libs -g';
            $config{'llibs'}        = '-Wl,-Bstatic -lapr-1 -Wl,-Bdynamic -lpthread -lm';
            $config{'make'}         = 'make';
            $config{'exe'}          = '';
            $config{'o'}            = '.o';
            $config{'rm'}           = 'rm -f';
            $config{'noreturn'}     = '';
            $config{'noreturngcc'}  = '__attribute__((noreturn))';
            $config{'cat'}          = 'cat';
        }
        else {
            return (excuse => 'So far, we only support building with clang or gcc on Linux.');
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
