# target=test_mem.o
# 
# all: server client clear
# server: tree_map.o tree_hash_map.o rb_tree.o hash_map2.o hash_map.o hash.o $(target) 
# 	gcc -o server rb_tree.o tree_map.o tree_hash_map.o hash_map2.o hash_map.o hash.o $(target) -lpthread 
# client: client.o operations.o 
# 	gcc -o client client.o operations.o -pthread
# rb_tree.o: rb_tree.c
# test_server.o: test_server.c   #local server
# server.o: server.c
# test_mem.o: test_mem.c
# client.o: client.c
# operations.o: operations.c
# tree_map.o: tree_map.c
# tree_hash_map.o: tree_hash_map.c
# hash_map2.o: hash_map2.c
# hash_map.o: hash_map.c
# hash.o: hash.c
# 
# clear:
# 	rm -v *.o
# 
# clear:
# 	${RM} *.o
#

all: server client clear

server: server.o
	gcc -o server server.o -lpthread
client: client.o
	gcc -o client client.o -lpthread

server.o: server.c
client.o: client.c

clear:
	rm -v *.o
