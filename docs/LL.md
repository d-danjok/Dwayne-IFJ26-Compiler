1. \<program> ::= __import ifj26.*__ \<def_list>
2. \<def_list>  ::= \<func_def>\<def_list>
3. \<def_list>  ::= \<main_func>\<def_list>
4. \<def_list>  ::= __ε__
5. \<val_def> ::= __val__ \<def_body>
6. \<var_def> ::= __var__ \<def_body>
7. \<def_body> ::= ID
8. \<def_body> ::= ID __:__ TYPE
9. \<func_def>  ::= \<func_start>\<func_body>
10. \<main_func>  ::= __@main__ \<func_start>\<func_body>
11. \<main_func>  ::= __@main__ EOL \<func_start>\<func_body>
12. \<func_start>  ::= \<func_name>\<func_head>
13. \<func_head>  ::= __(__ \<param_list> __:__ TYP
14. \<func_head>  ::= (\<param_list>
15. \<func_name>  ::= __def__ ID
16. \<param_list>  ::= )
17. \<param_list>  ::= EOL\<param_list>
18. \<param_list>  ::= \<param>\<param_list’>
19. \<param_list’>  ::= __,__ \<param>\<param_list’>
20. \<param_list’>  ::= __,__ EOL \<param>\<param_list’>
21. \<param_list’>  ::= __,__ EOL __)__
22. \<param_list’>  ::= __)__
23. \<param>  ::= ID __:__ TYPE
24. \<func_body>  ::= __=__ \<block>
25. \<func_body> ::= __=__ \<expression>
26. \<func_body> ::= __=__ \<func_call>
27. \<block>  ::= EOL INDENT \<stat_list> DEDENT
28. \<stat_list> ::= \<statement>\<stat_list’>
29. \<stat_list’> ::= __;__ \<statement>\<stat_list’>
30. \<stat_list’> ::= EOL \<statement>\<stat_list’>
31. \<stat_list’> ::= __ε__
32. \<statement> ::= ID __=__ \<assign_val>
33. \<statement> ::= \<definition>
34. \<statement> ::= \<func_call>
35. \<statement> ::= \<expression>
36. \<statement> ::= \<while>
37. \<statement> ::= \<if_else>
38. \<statement> ::= __()__
39. \<statement> ::= \<return>
40. \<definition> ::= \<val_def> __=__ \<assign_val>
41. \<definition> ::= \<var_def> __=__ \<assign_val>
42. \<assign_val> ::= \<expression>
43. \<assign_val> ::= \<func_call>
44. \<func_call> ::= ID __(__\<call_params>
45. \<call_params> ::= __)__
46. \<call_params> ::= EOL \<call_params>
47. \<call_params> ::= \<call_param>\<call_params’>
48. \<call_params’> ::= __,__ \<call_param>\<call_params’>
49. \<call_params’> ::= __,__ EOL \<call_param>\<call_params’>
50. \<call_params’> ::= __,__ EOL )
51. \<call_params’> ::=  __)
52. \<call_param>  ::= LITERAL
53. \<call_param>  ::= ID
54. \<call_param>  ::= \<expression>
55. \<while> ::= __while__ \<binary_expression> __do__ \<block>
56. \<if_else> ::= __if__ \<binary_expression> __then__ \<block> __else__ \<block>
57. \<return> ::= __return__ \<assign_val>
58. \<return> ::= __return__