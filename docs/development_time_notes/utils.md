1) I have added some wrappers to memory allocation/free functions for more straightforward error handling
2) safe allocator's goal is to provide zeroed memory, safe realloc was not implemented because I can not see it's usage
   right now, and it is not clear how it should be created according to programming guideline
3) i have added get_char and peek_char for more straight-forward error handling and peek_char allows to check next
   character without consuming it