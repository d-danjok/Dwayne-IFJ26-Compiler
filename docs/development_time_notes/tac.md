1) I chose quadruple format of TAI over triples or indirect-truples, because for me, it is the easiest to implement and understand.
2) I created layout and definitions for the first iteration. Maybe gonna reimplement get_curr_instruction, as it is too strange and can be bad for optimalization phase.
3) I took inspiration of TAC instructions from the `Dragon Book`
4) Bool operators are splited to basic `>`, `<`, `==`, `!=`, for simplicity for ```codegen```