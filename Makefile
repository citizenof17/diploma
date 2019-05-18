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
# client: client.o
	# gcc -o client client.o -lpthread
# client.o: client.c

STD=-std=c11

all: server client clear

server: tree_hash_map.o rb_tree.o hash.o protocol.o server.o
	gcc ${STD} -o server tree_hash_map.o rb_tree.o hash.o protocol.o server.o -lpthread

client: protocol.o client.o
	gcc ${STD} -o client client.o protocol.o -lpthread

# client: client.o protocol.o
# 	gcc ${STD} -o client client.o protocol.o -lpthread

test: test.o
	gcc ${STD} -o test test.o -lpthread

protocol.o: protocol.c
server.o: server.c
client.o: client.c
test.o: test.c
tree_hash_map.o: tree_hash_map.c
hash.o: hash.c
rb_tree.o: rb_tree.c

clear:
	rm -v *.o
