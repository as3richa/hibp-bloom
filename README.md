# hibp-bloom

## Building

```sh
# Build bin/hipb-bloom.a with maximimum optimization, no assertions, no debug symbols
make
BUILD=release make

# Build bin/hibp-bloom.a with no optimization and with debugging symbols;
# this plays nice with valgrind
BUILD=debug make
BUILD=debug-valgrind make

# Build bin/hibp-bloom.a with maximum optimization, deubgging symbols, and -fsanitize=address;
# will not play nice with valgrind
BUILD=debug-asan make

# Build and run the test suite
make test

# Build the test suite and run it under valgrind
make test-valgrind

# Most comprehensive way of running tests
make clean
BUILD=debug-asan make test
make clean
BUILD=debug-valgrind make test-valgrind

# Mixing BUILD levels won't prevent compilation or linking, but for best results run
# make clean before switching BUILD levels. make test-valgrind will likely either fail
# entirely or report spurious errors unless the library and tests were built under
# BUILD=debug-valgrind
```
