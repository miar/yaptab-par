%:- yap_flag(tabling_mode,[local,load_answers,local_trie]).

:-['data_edge_grid_35.pl'].

%edge(1,2).
%edge(1,3).

:- table path/2.
path(X, Z):- edge(X, Z).
path(X, Z):- edge(X, Y), path(Y, Z).

go_parallel:- parallel(path(X,Y)),
       fail.

go_parallel.


go_single:- path(X,Y),
	    fail.
go_single.


%:- go_single.

%:- tabling_statistics.

:- parallel_mode(on).

:- go_parallel.
:- show_all_tables.
:-halt.

% ./yap -w 4 % com 4 workers
