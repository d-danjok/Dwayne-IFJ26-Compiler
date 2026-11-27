# Built-in function integration tests

Run from the repository root:

```sh
python3 tests/builtins/run.py
python3 tests/builtins/run.py -k ord
python3 tests/builtins/run.py --interpreter '/path/to/ic26int {source}'
```

The runner builds the current compiler in a temporary directory, opens each `.ifj26` file as seekable standard input, then compares interpreter exit code and exact output. The seekable input matters: the current lexer uses `ftell`/`fseek` for lookahead and returns internal error 99 when source arrives through a pipe. Without `--interpreter`, the runner uses the repository's limited IFJcode26 test executor; use the official interpreter for a definitive result. Build products and generated code are kept outside the repository and removed after the run.

The 44 cases use 30 source files. They follow §4 of `Specifikace_IFJ26_a_IFJcode26.pdf` for built-ins, §5.1–5.2 for FUNEXP and BOOLTHEN, and §2.6 of the main assignment for end-to-end black-box testing. Each failure prints the expected and actual compiler/interpreter exits and output, then explains the stage and likely operation. String cases cover `ord` at negative, empty, and past-end indices; `subStr` empty and full slices plus invalid bounds; and `chr` outside ASCII. Input cases check `readInt`, `readDouble`, `readLine`, and `readBoolean` with valid, invalid, and EOF input. Conversion cases check `toStr`, `toInt`, `chr`, and `strLength`. Integrated cases combine built-ins with user-defined functions, input order, arithmetic, control flow, and Boolean short-circuiting. Five semantic cases require compiler exit 5 for invalid built-in calls.

The runner also compiles a minimal baseline program. If that returns a nonzero code, failures are reported at the compiler stage because no built-in or interpreter behavior can be observed yet.
