1) I chose to use token type with same attribute type, for me it seems way easier from memory management point,
   because if I would choose separate token attribute types, in case of string values or identifier names
   I would need to allocate and free memory for each token. Also as it was mentioned in [bst.md](bst.md), later
   identifiers'
   indexes in attribute table can be used in symbol table, so I see way too much benefits towards this design decision
2) once <u>dynamically allocated</u> string value or identifier name inserted into attribute table, attribute table
   becomes owner of those values, it simplifies memory management because everything is freed in one place
3) table lacks an ability to remove values, this is intentional. Since values are accessed by index any removal would
   lead to index shift, which is not what we want