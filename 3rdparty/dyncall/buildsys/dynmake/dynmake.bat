cl /nologo /DMAKE_CMD_%~n2 /EP Makefile.M 1> Makefile.dynmake
%2 /NOLOGO /f Makefile.dynmake %1
