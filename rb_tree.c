#include "rb_tree.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

Tree* createTree() {
  Tree *tr = (Tree*)malloc(sizeof(Tree));
  tr->nil = makeNewNode(tr, "nil", "nil");
  tr->root = tr->nil;
  return tr;
}

Node* makeNewNode(Tree *T, char *key, char *value) {
  Node* nd = (Node*)malloc(sizeof(Node));
  nd->left = nd->right = T->nil;
  nd->p = T->nil;
  nd->key = (char*)malloc(strlen(key) + 1);
  nd->value = (char*)malloc(strlen(value) + 1);
  strcpy(nd->key, key);
  strcpy(nd->value, value);
  
  // nd->key = key;
  // nd->value = value;
  nd->color = 'b';    //added

  return nd;
}

Node* treeSearch(Tree *T, Node *x, char *key) {  //x is a root of the subtree
  if (x == T->nil || strcmp(key, x->key) == 0) {
    return x;
  }

  // if (k < x->key) {
  if (strcmp(key, x->key) < 0){
    return treeSearch(T, x->left, key);  //find in the left child
  }
  else {
    return treeSearch(T, x->right, key);  //find in the right child
  }
}

Node* treeMinimum(Tree *T, Node *x) {
  while(x != T->nil && x->left != T->nil) {   
    x = x->left;
  }
  return x;
}

Node* treeMaximum(Tree *T, Node *x) {
  while(x != T->nil && x->right != T->nil) {
    x = x->right;
  }
  return x;
}

void leftRotate(Tree *T, Node *x) {
  Node *y = x->right;   
  x->right = y->left;   //transfer left y son to right x's

  if (y->left != T->nil) {
    y->left->p = x;    //change y's left son's parent 
  }
  y->p = x->p;    //change y's parent

  if (x->p == T->nil) { 
    T->root = y;   //new tree root
  }
  else{
    if (x == x->p->left) {
      x->p->left = y;   //x's parent's left son is y 
    }
    else {
      x->p->right = y;  //x's parent's right son is y 
    }
  }

  y->left = x; 
  x->p = y;
}

void rightRotate(Tree *T, Node *x) { //same as left rotate but all lefts changed to rights
  Node *y = x->left;
  x->left = y->right;

  if (y->right != T->nil){
    y->right->p = x;
  }
  y->p = x->p;

  if (x->p == T->nil){
    T->root = y;
  }
  else{
    if (x == x->p->right){
      x->p->right = y;
    }
    else{
      x->p->left = y;
    }
  }

  y->right = x;
  x->p = y;
}

void insertFixup(Tree *T, Node *z) {
  while (z->p->color == 'r') {   //while parent is red
    if (z->p == z->p->p->left) { //case for left
      Node *y = z->p->p->right; //uncle of z
      if (y->color == 'r') {     //1# case, uncle is red 
        z->p->color = 'b';      //change p's and uncles's color
        y->color = 'b';
        z->p->p->color = 'r';   //change p's parent's color
        z = z->p->p;            //do fixup for p's parent
      }
      else {
        if (z == z->p->right) {
          z = z->p;             //2# case, uncle is black
          leftRotate(T, z);     // change z and parent
        }
        z->p->color = 'b';      //3# case,  change parent's color
        z->p->p->color = 'r';   // change pp's color
        rightRotate(T, z->p->p); //rotate, parent is now black node
      } 
    }
    else {                       //same for right
      Node *y = z->p->p->left;
      if (y->color == 'r') {
        z->p->color = 'b';
        y->color = 'b';
        z->p->p->color = 'r';
        z = z->p->p;
      }
      else {
        if (z == z->p->left) {
          z = z->p;
          rightRotate(T, z);
        }
        z->p->color = 'b';
        z->p->p->color = 'r';
        leftRotate(T, z->p->p);
      }
    }
  }
  T->root->color = 'b';
}

void insert(Tree *T, char *key, char *value) {  //rb-insert changes value if key exists
  Node *y = T->nil;
  Node *x = T->root;
  Node *z = makeNewNode(T, key, value);

  while (x != T->nil) { //looking for new place
    y = x;
    // if (z->key < x->key){
    int cmp = strcmp(z->key, x->key);
    if (cmp < 0){
      x = x->left;
    }
    else if (cmp > 0) {
      x = x->right;
    }
    else { //z->key == x->key -- change x->value
      free(x->value);
      x->value = (char *)malloc(strlen(z->value) + 1);
      strcpy(x->value, z->value);
      free(z->key);
      free(z->value);
      free(z);
      // x->value = z->value; //?????
      return;
    }
  }

  z->p = y;   //y is new parent

  if (y == T->nil) {
    T->root = z;  //z is root
  }
  else {
    // if (z->key < y->key){  //is z left or right son?
    if (strcmp(z->key, y->key) < 0){
      y->left = z;
    }
    else {
      y->right = z;
    }
  }
  z->left = T->nil;  //no left child
  z->right = T->nil; //no right child
  z->color = 'r';    

  insertFixup(T, z);
}

void transplant(Tree *T, Node *u, Node *v) { //delete u, place v
  if (u->p == T->nil) {
    T->root = v;
  }
  else {
    if (u == u->p->left) {
      u->p->left = v;
    }
    else {
      u->p->right = v;
    }
  }
  v->p = u->p;
}


//we have extra "black"
//properties will be restored if: 
//#1 x is red-black node then we make it just black
//or #2 x is root - make x just black
//or #3 make rotations and coloring exit cycle
void deleteFixup(Tree *T, Node *x) {
  while (x != T->root  && x->color == 'b') {
    //x is always double blacked

    if (x == x->p->left) {  //if x is left son
      Node *w = x->p->right;  //second x->parent's son
      if (w->color == 'r') {  //case #1  -- brother is red
        w->color = 'b';      //change brother's color
        x->p->color = 'r';   //change parent's color
        leftRotate(T, x->p);  //w is new pp for x
        w = x->p->right;     //x's brother is black now
      }
      if (w->left->color == 'b' && w->right->color == 'b') {  //case #2
        w->color = 'r';
        x = x->p;
      }
      else {
        if (w->right->color == 'b') {  //case #3 w is black, w's left son is red, w's right son is black
          w->left->color = 'b';
          w->color = 'r';
          rightRotate(T, w);  //w-left is new x's brother
          w = x->p->right;     //moved to 4th case
        }
        w->color = x->p->color;    //case #4 w is black, w's right son is red
        x->p->color = 'b';   
        w->right->color = 'b';
        leftRotate(T, x->p);
        x = T->root;  //End of cycle operation
      }
    }
    else {   //same for right
      Node *w = x->p->left;
      if (w->color == 'r') {
        w->color = 'b';
        x->p->color = 'r';
        rightRotate(T, x->p);
        w = x->p->left;
      }
      if (w->right->color == 'b' && w->left->color == 'b') {
        w->color = 'r';
        x = x->p;
      }
      else {
        if (w->left->color == 'b') {
          w->right->color = 'b';
          w->color = 'r';
          leftRotate(T, w);
          w = x->p->left;
        }
        w->color = x->p->color;
        x->p->color = 'b';
        w->left->color = 'b';
        rightRotate(T, x->p);
        x = T->root;
      }
    }
  }
  x->color = 'b';
}

void erase(Tree *T, char *key) {
  Node *z = treeSearch(T, T->root, key);
  Node *x = NULL;   //this node will replace y

  if (z != T->nil) {
    Node *y = z;                      //y "will be" deleted
    char yColor = y->color;           //save y's color
    if (z->left == T->nil) {          //only lright son exists
      x = z->right;              
      transplant(T, z, z->right);
    }
    else {
      if (z->right == T->nil) {         //only left son exists
        x = z->left;
        transplant(T, z, z->left);
      }
      else {                          //both sons exist
        y = treeMinimum(T, z->right); //find node to replace x
        yColor = y->color;
        x = y->right;
        if (y->p == z) {
          x->p = y;
        }
        else {
          transplant(T, y, y->right);
          y->right = z->right;
          y->right->p = y;
        }
        transplant(T, z, y);
        y->left = z->left;
        y->left->p = y;
        y->color = z->color;
      }
    }
  
    if (yColor == 'b') { //if y != z then replacing y Can lead to a violation of properties of rbt
      deleteFixup(T, x);
    }

    free(z->key);
    free(z->value);    
    free(z);
  }
}

void clearTree(Tree *T, Node *t) {
  if (t == NULL || t == T->nil) {
    return;
  }

  clearTree(T, t->left);
  clearTree(T, t->right);
  free(t->key);
  free(t->value);  
  free(t);
}

void deleteTree(Tree *T){
  clearTree(T, T->root);
  free(T->nil->key);
  free(T->nil->value);
  free(T->nil);  
}

void printTree(Node *q, long n) { ////auxiliary function for testing
   long i;
   if (q) {
      printTree(q->right, n+5);
      for (i = 0; i < n; i++) {
        printf(" ");
      }
      printf("%s %s %c\n", q->key, q->value, q->color);
      printTree(q->left, n+5);
   }
}

int contains(Tree *T, Node *x, char *key) {  //x is a root of the subtree
  Node* t = treeSearch(T, x, key);
  return t != T->nil;
}

int getValue(Tree *T, char *key, char *value) { //return null if not exists?
  Node* t = treeSearch(T, T->root, key);
  strcpy(value, t->value);
  return 0;
}