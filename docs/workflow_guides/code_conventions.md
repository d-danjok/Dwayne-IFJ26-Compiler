# Code conventions

## Variables & Constants

* snake_case
* Makros and enum member names should be capitalised 
    ```c
    #define FUNC_NAME "name" 
    
    typedef enum{
        NONE,
        INTERNAL_ERR,
        IO_ERR
        ...
    } error;
    ```

* Name of variable should give full understanding of what information is stored in it, no names like: `str_st_buf` (string storage buffer) instead white smth like `str_storage_buffer`, while we do not write full `string`, `str` is quite widely used abbreviation  

* ALWAYS initialise variable with zero-equivalent contents do not leave it "empty" (cuz it has some random data in it by default)
    ```c
    //CORRECT
    int num = 0;
    float division_result = 0.0;
    char* message = NULL;
  
    //WRONG
    char* err_message;
    unsigned index; 
    ```

## Functions

* Functions which work as method of some structure should follow this naming scheme
    ```c
    void <structure name>_function(<structure name>* self, ...);
    ```

* If function could possibly fail, it should have pointer to error receiving variable (initialised and set to no error state in advance) as first argument
    ```c
    void function(error_code* err, ...)
    ```
  After each call error should be explicitly checked
    ```c
    error err = NONE;
  
    void function(&err, ...);
    if (err != NONE)
    {
        //handle error
    }
    ```
    * In case if function both works as method of some struct and could fail pointer to struct will be first argument, and pointer to error receiving variable will be second
    ```c
    void <structure name>_function(<structure name>* self, error_code* err, ...);
    ```
## Enums 
* Each enum should have it`s own typedef in order to make code more readable
* Enum member names should be capitalised (check [Variables & Constants](#variables--constants)) 

## Comments
* Every function in header file should have Doxygen comment.
* Generally try to reduce amount of comments, especially ones which describe what code does, code should be clear enough to be understandable without comments, in order to achieve this try to follow technics shows in this [video](https://youtu.be/Bf7vDBBOBUA) 

## Structure
* Use early return (inversion) and extraction technics from this [video](https://youtu.be/CFRhGnuXG-4?si=lyEQnGmjjeNmXdk6)


## Formatting

* Basically use **Microsoft** formatting style, IDE can be setup to check it, but clang-format should be used to ensure it

## Memory management

* Avoid dynamic memory allocation, use it only if it is absolutely impossible not to use it

## ...