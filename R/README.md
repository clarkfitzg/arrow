## Development install

_Tested on Mac and Ubuntu_

First [install Arrow](https://github.com/apache/arrow/tree/master/cpp) and
the dependencies.

Assuming that you're in the same directory as the arrow source run these
commands:

```

mkdir arrow_release
cd arrow_release
cmake ../arrow/cpp -DCMAKE_BUILD_TYPE=Release
make unittest
make install

```

To install Rarrow:

```

cd ../arrow/R
autoconf
./configure
R CMD INSTALL .

```
