#pragma once

typedef struct Node {
  char *key;
  char *value;
  char color;
  struct Node *left;
  struct Node *right;
  struct Node *p;
} Node;

typedef struct Tree {
  Node *root;
  Node *nil;
} Tree;

Tree* createTree();
Node* makeNewNode(Tree *T, char *key, char *value);
Node* treeSearch(Tree *T, Node *x, char *key);  //x is a root of the subtree
Node* treeMinimum(Tree *T, Node *x);
Node* treeMaximum(Tree *T, Node *x);

void leftRotate(Tree *T, Node *x);
void rightRotate(Tree *T, Node *x); //same as left rotate but all lefts changed to rights
void insertFixup(Tree *T, Node *z);
void insert(Tree *T, char *key, char *value);  //rb-insert
void transplant(Tree *T, Node *u, Node *v); //delete u, place v
void deleteFixup(Tree *T, Node *x);
void erase(Tree *T, char *key);
void clearTree(Tree *T, Node *t);
void deleteTree(Tree *T);
void printTree(Node *q, long n);
int  contains(Tree *T, Node* x, char *key);
int  getValue(Tree *T, char *key, char *value);


  // gcc -std=c99 rb_tree.c -lm
  //valgrind --leak-check=full --leak-resolution=med  ... 
