# Universal Lambda interpreter

This is an interprefer of the [Universal Lambda](http://www.golfscript.com/lam/)
programming language, written in C.

The implementation is based on my [Lazy K interpreter](https://github.com/irori/lazyk).
Internally it compiles the program into an SKI combinator expression, and
evaluates it in a similar way to the Lazy K interpreter.

## Usage

```sh
$ clamb [options] input-file...
```

Like the original `lamb` interpreter, `clamb` does not distinguish between
source files and input. It parses a program from the concatenation of all
_input-files_ and stdin at the end, and the remaining bytes are treated as
input.

Options:
- `-h`: Print help and exit.
- `-u`: Disable stdout buffering.
- `-p`: Parse the program, print it and exit.
- `-v`: Print version and exit.
- `-v0` (default): Do not print any debug information.
- `-v1`: Print some statistics after execution.
- `-v2`: Print logs for garbage collections.

## License

This software is released under the [MIT License](LICENSE).
