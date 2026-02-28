# eaux/go/errors/Panic

This repository provides some small, composable functions for translating errors into panics.

Why might you want to do this? Perhaps you're trying to call a function "A" and pass the result of another function "B" to "A". Unfortunately, B returns resultType, error, and so you can't pass it to "A", which expects only resultType.

Concretely:

```
func A(input string) {
  ...
}

func B(input string) (string, error) {
  ...
}

func C(input string) {
  A(B(input)) // won't compile, because B returns "string, error" and A only accepts "string."
}
```

Now, maybe you're confident that B will never return an error for the input you provide, or you just want that to mean "panic!" In that scenario, you can use this package:

```

import "github.com/earlye/eaux/go/errors/Panic"

func A(input string) {
  ...
}

func B(input string) (string, error) {
  ...
}

func C(input string) {
  A(Panic.Panic1(B(input))) // string result flows to A, or a panic() results.
}

```