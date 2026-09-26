1) I have added some wrappers for more straightforward error handling
2) safe allocator's goal is to provide zeroed memory, safe realloc was not implemented because I can not see it's usage
   right now, and it is not clear how it shold be created according to programming guideline