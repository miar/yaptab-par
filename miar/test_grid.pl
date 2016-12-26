:- yap_flag(tabling_mode,[local,load_answers,local_trie]).

:-['data_edge_grid_35.pl'].

%edge(1,2).
%edge(2,3).
%edge(3,1).

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



:- parallel_mode(on).

:- go_parallel.
%:- show_all_tables.
:- tabling_statistics.
:-halt.

% ./yap -w 4 % com 4 workers
./yap -l ../yaptab-par/miar/test_grid.pl -w 8 -s 40000 -h 300000 -t 80000

