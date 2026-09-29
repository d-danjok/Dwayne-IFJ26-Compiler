1) implemented generic BST(AVL) with integer key and void* data with an intention to use index of identifier in
   attribute_table as a key. I chose this option because it is much easier from implementation point of view
   (it is way easier to compare integers, than strings), and it allows to only allocate space for
   identifier name once (in attribute_table) and free only when this value is not needed anywhere
2) when data is inserted into BST, table takes ownership of this memory (it means this memory should be freed by BST)
3) I really wanted to make operations atomic, and it worked quite well util I needed to implement rotations and removal,
   so I needed to return pointer to new sub-root. But rest of functions remain void return 