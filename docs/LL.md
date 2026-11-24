## General grammar

1. \<opt_eol> ::= EOL
2. \<opt_eol> ::= __ε__

3. \<program> ::= __import ifj26.*__ EOL \<def_list>
4. \<def_list>  ::= \<func_def>\<def_list>
5. \<def_list>  ::= \<main_func>\<def_list>
6. \<def_list>  ::= __ε__
7. \<val_def> ::= __val__ \<def_body>
8. \<var_def> ::= __var__ \<def_body>
9. \<def_body> ::= ID \<opt_type>
10. \<type_notation> ::= __:__ TYPE
11. \<opt_type> ::= \<type_notation>
12. \<opt_type> ::= __ε__

13. \<func_def>  ::= \<func_start>\<func_body>
14. \<main_func>  ::= __@main__\<opt_eol> \<func_start>\<func_body>
15. \<func_start>  ::= \<func_name>\<func_head>
16. \<func_head>  ::= __(__ \<param_list> \<opt_type>
17. \<func_name>  ::= __def__ ID

18. \<param_list>  ::= )
19. \<param_list>  ::= EOL \<param_list>
20. \<param_list>  ::= \<param>\<param_list’>
21. \<param_list’>  ::= __,__ \<params'_tail>
22. \<param_list’>  ::= __)__
23. \<params'_tail>  ::= \<opt_eol> \<param>\<param_list’>
24. \<params'_tail>  ::= EOL __)__
25. \<param>  ::= ID \<type_notation>

26. \<func_body>  ::= __=__ \<block>
27. \<func_body> ::= __=__ \<expression> EOL
28. \<block>  ::= EOL INDENT \<stat_list> DEDENT
29. \<stat_list> ::= \<statement>\<stat_list’>

30. \<opt_stats> ::= \<statement>\<stat_list’>
31. \<opt_stats> ::= __ε__
32. \<stat_list’> ::= __;__ \<opt_stats> 
33. \<stat_list’> ::= EOL \<opt_stats>
34. \<statement> ::= \<definition>
35. \<statement> ::= \<expression>
36. \<statement> ::= \<while>
37. \<statement> ::= \<if_list>
38. \<statement> ::= \<return>

39. \<definition> ::= \<val_def> __=__ \<expression>
40. \<definition> ::= \<var_def> __=__ \<expression>
41. \<while> ::= __while__ \<expression> __do__ \<block>
42. \<if_list> ::= \<if>\<if_list'>
43. \<if_list'> ::= \<else>\<if_list'>
44. \<if_list'> ::= __ε__
45. \<if> ::= __if__ \<expression> __then__ \<block>
46. \<else> ::= __else__ \<if>
47. \<else> ::= __else__ \<block>

48. \<return> ::= __return__ \<return_tail>
49. \<return_tail> ::= \<expression>
50. \<return_tail> ::= __ε__

## Func-call grammar 
> NOTE: Following will be used inside of expressions

1. \<func_call> ::= ID __(__\<call_params>
2. \<call_params> ::= __)__
3. \<call_params> ::= EOL \<call_params>
4. \<call_params> ::= \<call_param>\<call_params’>
5. <call_params'> ::= , <call'_tail>
6. <call_params'> ::= )
7. <call'_tail>   ::= EOL <call_eol>
8. <call'_tail>   ::= <call_param> <call_params'>
9. <call_eol>     ::= )
10. <call_eol>    ::= <call_param> <call_params'>
11. <call_param>  ::= <expression>