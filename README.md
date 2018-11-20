# hibp-bloom

A small, ergonomic, and extensively tested library ~~and command-line tool~~ for constructing and querying [Bloom filters](https://en.wikipedia.org/wiki/Bloom_filter).

## Why?

`hibp-bloom` was catalyzed by [Have I Been Pwned](https://haveibeenpwned.com). Have I Been Pwned implements a service that allows one to check whether some password is present in a known data breach. Password reuse is risky in general, but is even more so if the reused password is known to be compromised; the ability to check whether a password is compromised is hence extremely useful.

HIBP has been genereous in publishing an anonymized version of their password database; each password is hashed, which makes it possible to check whether a given password is present in the set, however it isn't possible to enumerate the passwords or extract personal information.

500M+ hashes takes a lot of space on disk; the latest SHA1 dump is north of 9GB. Under the assumption that we only care about whether or not a given password has been compromised (ignoring interesting metadata like prevalence), it's possible to encode the set of compromised passwords as a Bloom filter. In fact, a 20MB Bloom filter can answer queries on such a dataset with no false negatives whatsoever and only a 0.1% false positive rate.

## API

The API is documented in great extensive detail in `include/hibp-bloom.h`.

## Build Process

```sh
# Build bin/hipb-bloom.a with maximimum optimization, no assertions, no debug
# symbols
make
BUILD=release make

# Build bin/hibp-bloom.a with no optimization and with debugging symbols;
# this plays nice with valgrind
BUILD=debug make
BUILD=debug-valgrind make

# Build bin/hibp-bloom.a with maximum optimization, deubgging symbols, and
# -fsanitize=address; will not play nice with valgrind
BUILD=debug-asan make

# Build with -flto. Plays nice with any BUILD level
LTO=1 make

# Build and run the test suite
make test

# Build the test suite and run it under valgrind
make test-valgrind

# Most comprehensive way of running tests
make clean
BUILD=debug-asan make test
make clean
BUILD=debug-valgrind make test-valgrind

# Mixing BUILD levels won't necessarily prevent compilation or linking, but for
# best results you should make clean before switching BUILD levels. make test-valgrind
# will likely either fail entirely or report spurious errors unless everything was
# built under BUILD=debug-valgrind
```

## Future Work

- Command-line application for manipulating on-disk Bloom filters directly
- Bindings for popular languages
- Prebuilt filters for the HIBP pwned password dataset

## License

Copyright 2018 Adam Richardson (@as3richa)

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
