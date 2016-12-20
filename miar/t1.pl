
:-table path/2.

path(1,2) :- writeln(ola).

go :- parallel(path(X,Y)),
             fail.
go.

:- go, show_all_tables.
:- halt.
