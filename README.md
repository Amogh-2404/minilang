# minilang

A small statically-typed language compiled to native code via LLVM. Toy
project; details land as the build progresses.

```
cmake -G Ninja -B build && ninja -C build
echo "1+2;" | ./build/minilang
```
