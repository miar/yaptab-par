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
% ./yap -l ../yaptab-par/miar/test_grid.pl -w 8 -s 40000 -h 300000 -t 80000
/* tabling_statistics with one worker
Execution data structures
  Table entries:                           48 bytes (1 structs in use)
  Subgoal frames:                      107888 bytes (1226 structs in use)
  Dependency frames:                       64 bytes (1 structs in use)
  Memory in use (I):                   108000 bytes

Local trie data structures
  Subgoal trie nodes:                   98120 bytes (2453 structs in use)
  Answer trie nodes:                168207256 bytes (3003701 structs in use)
  Subgoal trie hashes:                     32 bytes (1 structs in use)
  Answer trie hashes:                   98040 bytes (2451 structs in use)
  Memory in use (II):               168403448 bytes

Global trie data structures
  Global trie nodes:                       40 bytes (1 structs in use)
  Global trie hashes:                       0 bytes (0 structs in use)
  Memory in use (III):                     40 bytes

Total memory in use (I+II+III):     168511488 bytes
*/
