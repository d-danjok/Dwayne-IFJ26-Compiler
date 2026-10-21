1. \<program> ::= __import ifj26.*__ EOL \<def_list>
2. \<def_list>  ::= \<func_def>\<def_list>
3. \<def_list>  ::= \<main_func>\<def_list>
4. \<def_list>  ::= __ε__
5. \<val_def> ::= __val__ \<def_body>
6. \<var_def> ::= __var__ \<def_body>
7. \<def_body> ::= ID \<type_notation>
8. \<type_notation> ::= __:__ TYPE
9. \<type_notation> ::= __ε__
10. \<func_def>  ::= \<func_start>\<func_body>
11. \<main_func>  ::= __@main__ \<func_start>\<func_body>
12. \<main_func>  ::= __@main__ EOL \<func_start>\<func_body>
13. \<func_start>  ::= \<func_name>\<func_head>
14. \<func_head>  ::= __(__ \<param_list> __:__ TYP
15. \<func_head>  ::= (\<param_list>
16. \<func_name>  ::= __def__ ID
17. \<param_list>  ::= )
18. \<param_list>  ::= EOL\<param_list>
19. \<param_list>  ::= \<param>\<param_list’>
20. \<param_list’>  ::= __,__ \<param>\<param_list’>
21. \<param_list’>  ::= __,__ EOL \<param>\<param_list’>
22. \<param_list’>  ::= __,__ EOL __)__
23. \<param_list’>  ::= __)__
24. \<param>  ::= ID __:__ TYPE
25. \<func_body>  ::= __=__ \<block>
26. \<func_body> ::= __=__ \<expression>
27. \<block>  ::= EOL INDENT \<stat_list> DEDENT
28. \<stat_list> ::= \<statement>\<stat_list’>
29. \<stat_list’> ::= __;__ \<statement>\<stat_list’>
30. \<stat_list’> ::= EOL \<statement>\<stat_list’>
31. \<stat_list’> ::= __ε__
32. \<statement> ::= ID __=__ \<assign_val>
33. \<statement> ::= \<definition>
34. \<statement> ::= \<expression>
35. \<statement> ::= \<while>
36. \<statement> ::= \<if_else>
37. \<statement> ::= \<return>
38. \<definition> ::= \<val_def> __=__ \<assign_val>
39. \<definition> ::= \<var_def> __=__ \<assign_val>
40. \<assign_val> ::= \<expression>
41. \<while> ::= __while__ \<expression> __do__ \<block>
42. \<if_else> ::= __if__ \<expression> __then__ \<block> __else__ \<block>
43. \<return> ::= __return__ \<assign_val>
44. \<return> ::= __return__

NOTE: Following will be used inside of expressions

45. \<func_call> ::= ID __(__\<call_params>
46. \<call_params> ::= __)__
47. \<call_params> ::= EOL \<call_params>
48. \<call_params> ::= \<call_param>\<call_params’>
49. \<call_params’> ::= __,__ \<call_param>\<call_params’>
50. \<call_params’> ::= __,__ EOL \<call_param>\<call_params’>
51. \<call_params’> ::= __,__ EOL )
52. \<call_params’> ::=  __)
53. \<call_param>  ::= \<expression>