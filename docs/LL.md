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
31. \<stat_list’> ::= __;__
32. \<stat_list’> ::= EOL
33. \<statement> ::= ID __=__ \<assign_val>
34. \<statement> ::= \<definition>
35. \<statement> ::= \<expression>
36. \<statement> ::= \<while>
37. \<statement> ::= \<if_list>
38. \<statement> ::= \<return>
39. \<definition> ::= \<val_def> __=__ \<assign_val>
40. \<definition> ::= \<var_def> __=__ \<assign_val>
41. \<assign_val> ::= \<expression>
42. \<while> ::= __while__ \<expression> __do__ \<block>

[//]: # (BOOLTHEN extension)

43. \<if_list> ::= \<if>\<if_list'>
44. \<if_list'> ::= \<else>\<if_list'>
45. \<if_list'> ::= __ε__
46. \<if> ::= __if__ \<expression> __then__ \<block>
47. \<else> ::= __else__ \<if>
48. \<else> ::= __else__ \<block>

49. \<return> ::= __return__ \<assign_val>
50. \<return> ::= __return__

NOTE: Following will be used inside of expressions

1. \<func_call> ::= ID __(__\<call_params>
2. \<call_params> ::= __)__
3. \<call_params> ::= EOL \<call_params>
4. \<call_params> ::= \<call_param>\<call_params’>
5. \<call_params’> ::= __,__ \<call_param>\<call_params’>
6. \<call_params’> ::= __,__ EOL \<call_param>\<call_params’>
7. \<call_params’> ::= __,__ EOL )
8. \<call_params’> ::=  __)
9. \<call_param>  ::= \<expression>